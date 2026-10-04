package chipyard

import chisel3._
import chisel3.util._
import org.chipsalliance.cde.config.{Config, Parameters}
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.tilelink._
import freechips.rocketchip.regmapper._
import freechips.rocketchip.subsystem._
import chipyard.example._
import chipyard.iobinders.{OverrideIOBinder, Port}
import chipyard.harness.{HarnessBinder, HasHarnessInstantiators}
import icenet._

// RV32 descriptor ABI: address staging then length commit; RX address commits
// directly. The data plane is the same coherent IceNIC DMA used on the FPGA.
class MikosNIC(implicit p: Parameters) extends LazyModule {
  val node = TLRegisterNode(Seq(AddressSet(0x10016000, 0xfff)),
    new SimpleDevice("mikos-nic", Seq("mikos,packet-dma-v1")), beatBytes = 4)
  val send = LazyModule(new IceNicSendPath(0))
  val recv = LazyModule(new IceNicRecvPath())
  val dma = TLIdentityNode()
  dma := TLWidthWidget(8) := send.node
  dma := TLWidthWidget(8) := recv.node
  lazy val module = new Impl
  class Impl extends LazyModuleImp(this) {
    val io = IO(new EthernetPCIeIO)
    val address = RegInit(0.U(32.W))
    val txq = Module(new Queue(UInt(64.W), 16))
    val rxq = Module(new Queue(UInt(64.W), 16))
    val txdone = Module(new Queue(Bool(), 16))
    val rxdone = Module(new Queue(UInt(16.W), 16))
    send.module.io.send.req <> txq.io.deq
    txdone.io.enq <> send.module.io.send.comp
    recv.module.io.recv.req <> rxq.io.deq
    rxdone.io.enq <> recv.module.io.recv.comp
    send.module.io.rlimit.inc := 1.U
    send.module.io.rlimit.period := 0.U
    send.module.io.rlimit.size := 1.U
    val txCommit = Wire(Decoupled(UInt(32.W)))
    val rxCommit = Wire(Decoupled(UInt(32.W)))
    txq.io.enq.valid := txCommit.valid
    txq.io.enq.bits := Cat(0.U(1.W), txCommit.bits(14, 0), 0.U(16.W), address)
    txCommit.ready := txq.io.enq.ready
    rxq.io.enq.valid := rxCommit.valid
    rxq.io.enq.bits := rxCommit.bits
    rxCommit.ready := rxq.io.enq.ready
    val counts = Cat(rxdone.io.count.pad(8), txdone.io.count.pad(8),
      (16.U - rxq.io.count).pad(8), (16.U - txq.io.count).pad(8))
    node.regmap(
      0x00 -> Seq(RegField(32, address)),
      0x04 -> Seq(RegField.w(32, rxCommit)),
      0x08 -> Seq(RegField.w(32, txCommit)),
      0x0c -> Seq(RegField.r(32, counts)),
      0x10 -> Seq(RegField.r(1, txdone.io.deq)),
      0x14 -> Seq(RegField.r(16, rxdone.io.deq)),
      0x1c -> Seq(RegField.r(32, "h4d4e4931".U)))
    val tx = Module(new EthernetPacketRateLimiter(50))
    tx.io.in <> Queue(send.module.io.out, 2)
    io.tx <> tx.io.out
    val credits = RegInit(511.U(10.W))
    recv.module.io.in.valid := io.rx.valid && credits =/= 0.U
    recv.module.io.in.bits := io.rx.bits
    io.rx.ready := recv.module.io.in.ready && credits =/= 0.U
    credits := credits + recv.module.io.buf_free.head - io.rx.fire
    assert(credits <= 511.U)
    io.txFrames := 0.U; io.rxFrames := 0.U
    io.txBytes := 0.U; io.rxBytes := 0.U
  }
}

trait HasMikosNIC { this: BaseSubsystem =>
  val manager = locateTLBusWrapper(PBUS)
  val client = locateTLBusWrapper(FBUS)
  val domain = manager.generateSynchronousDomain.suggestName("mikos_nic_domain")
  val local = p.alterPartial { case NICKey => Some(NICConfig(
    inBufFlits = 512, outBufFlits = 512, ctrlQueueDepth = 16,
    checksumOffload = false, usePauser = false, packetMaxBytes = 1514)) }
  val nic = domain { LazyModule(new MikosNIC()(local)) }
  manager.coupleTo("mikos_nic") {
    nic.node := TLFragmenter(4, manager.blockBytes) := TLWidthWidget(manager.beatBytes) := _
  }
  client.coupleFrom("mikos_nic") { _ :=* nic.dma }
  val inner = domain { InModuleBody {
    val port = IO(new ClockedEthernetPCIeIO)
    port.clock := domain.clockBundle.clock
    port.reset := domain.clockBundle.reset.asBool
    port.bits <> nic.module.io
    port
  } }
  val mikos = InModuleBody {
    val port = IO(new ClockedEthernetPCIeIO)
    port <> inner
    port
  }
}
class MikosDigitalTop(implicit p: Parameters) extends DigitalTop with HasMikosNIC
case class MikosPort(getIO: () => ClockedEthernetPCIeIO) extends Port[ClockedEthernetPCIeIO]
class WithMikosIO extends OverrideIOBinder({ (system: HasMikosNIC) =>
  val port = IO(new ClockedEthernetPCIeIO).suggestName("mikos_net")
  port <> system.mikos
  (Seq(MikosPort(() => port)), Nil)
})
class MikosNetBridge extends BlackBox with HasBlackBoxResource {
  val io = IO(new Bundle {
    val clock = Input(Clock()); val reset = Input(Bool())
    val net = Flipped(new EthernetPCIeIO)
  })
  addResource("/vsrc/MikosNetBridge.sv")
}
class WithMikosBridge extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: MikosPort, chipId: Int) =>
    val bridge = Module(new MikosNetBridge)
    bridge.io.clock := port.io.clock
    bridge.io.reset := port.io.reset
    bridge.io.net <> port.io.bits
})
class WithMikosTop extends Config((site, here, up) => {
  case BuildSystem => (p: Parameters) => new MikosDigitalTop()(p)
})
class MikosRocketConfig extends Config(
  new WithMikosBridge ++ new WithMikosIO ++ new WithMikosTop ++
  new chipyard.config.WithUARTInitBaudRate(1000000) ++
  new chipyard.config.WithUARTFIFOEntries(8, 256) ++
  new chipyard.harness.WithHarnessBinderClockFreqMHz(50.0) ++
  new chipyard.config.WithUniformBusFrequencies(50.0) ++
  new freechips.rocketchip.rocket.WithRV32 ++
  new freechips.rocketchip.rocket.WithNBigCores(1) ++
  new chipyard.config.AbstractConfig)
