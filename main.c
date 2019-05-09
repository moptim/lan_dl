#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <microhttpd.h>

#define PORT 8080

struct fbuf {
	void *buf;
	size_t sz;
};

static struct MHD_Response *respond_download(struct fbuf *f,
                                             unsigned int *status)
{
	*status = MHD_HTTP_OK;
	return MHD_create_response_from_buffer(f->sz, f->buf,
	                                       MHD_RESPMEM_PERSISTENT);
}

static struct MHD_Response *respond_upload(const char *data,
                                           unsigned int *status)
{
	const char *rbuf = "Not implemented\n";

	*status = MHD_HTTP_BLOCKED_BY_WINDOWS_PARENTAL_CONTROLS;
	return MHD_create_response_from_buffer(strlen(rbuf), (void *)rbuf,
	                                       MHD_RESPMEM_PERSISTENT);
}

static struct MHD_Response *respond_wrong_method(unsigned int *status)
{
	const char *data = "Not implemented\n";

	*status = MHD_HTTP_UNAVAILABLE_FOR_LEGAL_REASONS;
	return MHD_create_response_from_buffer(strlen(data), (void *)data,
	                                       MHD_RESPMEM_PERSISTENT);
}

static int dispatch_connection(void *cls,
                               struct MHD_Connection *connection,
                               const char *url,
                               const char *method,
                               const char *version,
                               const char *upload_data,
                               size_t *upload_data_size,
                               void **con_cls)
{
	int rv;
	unsigned int status;
	struct MHD_Response *response;
	struct fbuf *f = cls;

	if (strcmp(method, "GET") == 0)
		response = respond_download(f, &status);
	else if (strcmp(method, "POST") == 0)
		response = respond_upload(upload_data, &status);
	else
		response = respond_wrong_method(&status);

	printf("Responding with %u\n", status);
	rv = MHD_queue_response(connection, status, response);
	MHD_destroy_response(response);
	return rv;
}

static int read_file(const char *fn, struct fbuf *f)
{
	int rv = 0, fd;
	ssize_t bytes_read;
	struct stat sbuf;

	if ((fd = open(fn, O_RDONLY)) < 0) {
		rv = -1;
		perror("open()");
		goto out;
	}
	if ((rv = fstat(fd, &sbuf)) < 0) {
		perror("fstat()");
		goto out_close_file;
	}
	f->sz = (size_t)(sbuf.st_size);
	if ((f->buf = malloc(f->sz)) == NULL) {
		rv = -1;
		perror("malloc()");
		goto out_close_file;
	}
	bytes_read = read(fd, f->buf, f->sz);
	if (bytes_read < f->sz) {
		rv = -1;
		fprintf(stderr, "Failed to read %zu bytes from %s (got %zu)\n",
		        f->sz, fn, bytes_read);
		goto out_free_buf;
	}
	goto out_success;

out_free_buf:
	free(f->buf);
out_success:
out_close_file:
	close(fd);
out:
	return rv;
}

static int wait_any_signal(void)
{
	int rv = 0, signo;
	sigset_t sigs;

	if ((rv = sigfillset(&sigs)) != 0) {
		perror("sigfillset()");
		goto out;
	}
	if ((rv = sigwait(&sigs, &signo)) != 0) {
		perror("sigwait()");
		goto out;
	}
out:
	return rv;
}

int main(int ar, char **av)
{
	int rv = 0;
	struct fbuf f;
	struct MHD_Daemon *daemon;

	if (ar != 2) {
		printf("Usage: %s <file to send>\n", av[0]);
		goto out;
	}
	if ((rv = read_file(av[1], &f)) != 0)
		goto out;

	daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
	                          &dispatch_connection, &f, MHD_OPTION_END);
	if (daemon == NULL) {
		rv = -1;
		fprintf(stderr, "Failed to create daemon\n");
		goto out_free_buf;
	}
	rv = wait_any_signal();

out_stop_daemon:
	MHD_stop_daemon(daemon);
out_free_buf:
	free(f.buf);
out:
	return rv;
}
