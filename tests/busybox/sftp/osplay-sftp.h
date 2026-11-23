#ifndef OSPLAY_SFTP_H
#define OSPLAY_SFTP_H

#include <stddef.h>

struct osplay_sftp;

struct osplay_sftp *osplay_sftp_new(const char *home);
void osplay_sftp_free(struct osplay_sftp *sftp);
/* Consumes at most one request, possibly fragmented across several feeds. */
size_t osplay_sftp_feed(struct osplay_sftp *sftp, const unsigned char *data, size_t size);
size_t osplay_sftp_output(const struct osplay_sftp *sftp, const unsigned char **data);
void osplay_sftp_sent(struct osplay_sftp *sftp, size_t size);
/* Nonzero for a fatal framing error, or EOF during an incomplete request. */
int osplay_sftp_failed(const struct osplay_sftp *sftp, int eof);

#endif
