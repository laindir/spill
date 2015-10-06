#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

struct buffer
{
	unsigned long size;
	unsigned long produced;
	unsigned long consumed;
};

int buffer_consume_at(const struct buffer *b)
{
	return b->consumed % b->size;
}

int buffer_produce_at(const struct buffer *b)
{
	return b->produced % b->size;
}

unsigned long buffer_data_available(const struct buffer *b)
{
	const unsigned long actual = b->produced - b->consumed;
	const unsigned long until_end = b->size - buffer_consume_at(b);
	return until_end < actual ? until_end : actual;
}

unsigned long buffer_space_available(const struct buffer *b)
{
	const unsigned long actual = b->size - (b->produced - b->consumed);
	const unsigned long until_end = b->size - buffer_produce_at(b);
	return until_end < actual ? until_end : actual;
}

struct file_buffer
{
	struct buffer buffer;
	int fd;
};

struct memory_buffer
{
	struct buffer buffer;
	char *data;
};

void
usage(void)
{
	fprintf(stderr, "Usage: spill file\n");
}

char buf[4096];
char mem[65536];

#define ARRSZ(arr) (sizeof(arr)/sizeof(arr[0]))

int
main(int argc, char *argv[])
{
	struct memory_buffer mb = {{ARRSZ(mem), 0, 0}, mem};
	struct file_buffer fb = {{-1, 0, 0}, -1};
	struct pollfd pfds[2] = {{STDIN_FILENO, POLLIN, 0}, {STDOUT_FILENO, 0, 0}};
	if(argc != 2)
	{
		usage();
		exit(EXIT_FAILURE);
	}
	fb.fd = open(argv[1], O_RDWR);
	if(fb.fd == -1)
	{
		perror("open");
		exit(EXIT_FAILURE);
	}
	do
	{
		int n = poll(pfds, 2, -1);
		if(n == -1)
		{
			perror("poll");
			exit(EXIT_FAILURE);
		}
		else if(pfds[1].revents & POLLOUT)
		{
			if(buffer_data_available(&mb.buffer))
			{
				int w = write(STDOUT_FILENO, mb.data + buffer_consume_at(&mb.buffer), buffer_data_available(&mb.buffer));
				if(w == -1)
				{
					perror("write");
					exit(EXIT_FAILURE);
				}
				mb.buffer.consumed += w;
			}
			else if(buffer_data_available(&fb.buffer))
			{
				int toread = ARRSZ(buf) < buffer_data_available(&fb.buffer) ? ARRSZ(buf) : buffer_data_available(&fb.buffer);
				int r = lseek(fb.fd, buffer_consume_at(&fb.buffer), SEEK_SET);
				int w = 0;
				if(r == -1)
				{
					perror("lseek");
					exit(EXIT_FAILURE);
				}
				r = read(fb.fd, buf, toread);
				if(r == -1)
				{
					perror("read");
					exit(EXIT_FAILURE);
				}
				while(r - w)
				{
					int t = write(STDOUT_FILENO, buf + w, r - w);
					if(t == -1)
					{
						perror("write");
						exit(EXIT_FAILURE);
					}
					w += t;
				}
				fb.buffer.consumed += w;
				pfds[0].events = POLLIN;
			}
			else
			{
				pfds[1].events = 0;
			}
		}
		else if(pfds[0].revents & POLLIN)
		{
			if(!buffer_space_available(&fb.buffer))
			{
				pfds[0].events = 0;
			}
			else if(!buffer_space_available(&mb.buffer) || buffer_data_available(&fb.buffer))
			{
				int toread = ARRSZ(buf) < buffer_data_available(&mb.buffer) ? ARRSZ(buf) : buffer_data_available(&mb.buffer);
				int r = read(STDIN_FILENO, buf, toread);
				int w = 0;
				if(r == -1)
				{
					perror("read");
					exit(EXIT_FAILURE);
				}
				if(r == 0)
				{
					pfds[0].events = 0;
				}
				while(r - w)
				{
					int t = write(fb.fd, buf + w, r - w);
					if(t == -1)
					{
						perror("write");
						exit(EXIT_FAILURE);
					}
					w += t;
				}
				fb.buffer.produced += w;
				pfds[1].events = POLLOUT;
			}
			else
			{
				int r = read(STDIN_FILENO, mb.data + buffer_produce_at(&mb.buffer), buffer_space_available(&mb.buffer));
				if(r == -1)
				{
					perror("read");
					exit(EXIT_FAILURE);
				}
				if(r == 0)
				{
					pfds[0].events = 0;
				}
				mb.buffer.produced += r;
				pfds[1].events = POLLOUT;
			}
		}
		else
		{
			fprintf(stderr, "Unexpected poll result: n = %d, stdin.revents = %hx, stdout.revents = %hx\n", n, pfds[0].revents, pfds[1].revents);
			exit(EXIT_FAILURE);
		}
	} while(pfds[0].events || pfds[1].events);
	exit(EXIT_SUCCESS);
}
