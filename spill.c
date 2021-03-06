#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

struct buffer
{
	unsigned long size;
	unsigned long produced;
	unsigned long consumed;
};

int buffer_consume_at(const struct buffer *b)
{
	return b->size ? b->consumed % b->size : 0;
}

int buffer_produce_at(const struct buffer *b)
{
	return b->size ? b->produced % b->size : 0;
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

struct options
{
	unsigned long memory;
	unsigned long filesize;
	int block;
	int abort;
	char *filename;
};

unsigned long hnum_parse(const char *num)
{
	unsigned long result;
	char *numstop;
	errno = 0;
	result = strtoul(num, &numstop, 0);
	if(errno != 0 || num == numstop)
	{
		usage();
		exit(EXIT_FAILURE);
	}
	else if(*numstop == 'k' || *numstop == 'K')
	{
		result *= 1024;
	}
	else if(*numstop == 'm' || *numstop == 'M')
	{
		result *= 1024 * 1024;
	}
	else if(*numstop == 'g' || *numstop == 'G')
	{
		result *= 1024 * 1024 * 1024;
	}
	return result;
}

struct options parse_options(int argc, char *argv[])
{
	struct options result = {0, -1, 0, 0, NULL};
	const char *optstring = "m:s:ba";
	int opt;
	while((opt = getopt(argc, argv, optstring)) != -1)
	{
		switch(opt)
		{
		case 'm':
			result.memory = hnum_parse(optarg);
			break;
		case 's':
			result.filesize = hnum_parse(optarg);
			break;
		case 'a':
			result.abort = 1;
			break;
		case 'b':
			result.block = 1;
			break;
		default:
			usage();
			exit(EXIT_FAILURE);
		}
	}
	if(result.filesize != ((unsigned long)-1) && !result.abort && !result.block)
	{
		fprintf(stderr, "When setting a maximum filesize, you must also specify either -a or -b\n");
		exit(EXIT_FAILURE);
	}
	if(optind >= argc)
	{
		usage();
		exit(EXIT_FAILURE);
	}
	result.filename = argv[optind];
	return result;
}

char buf[4096];

#define ARRSZ(arr) (sizeof(arr)/sizeof(arr[0]))

int
main(int argc, char *argv[])
{
	struct memory_buffer mb = {{0, 0, 0}, NULL};
	struct file_buffer fb = {{0, 0, 0}, -1};
	struct pollfd pfds[2] = {{STDIN_FILENO, POLLIN, 0}, {-1, POLLOUT, 0}};
	int flags;
	char *mem;
	struct options opts = parse_options(argc, argv);
	mem = malloc(opts.memory);
	if(mem == NULL)
	{
		fprintf(stderr, "Could not allocate requested memory\n");
		exit(EXIT_FAILURE);
	}
	mb.data = mem;
	mb.buffer.size = opts.memory;
	fb.buffer.size = opts.filesize;
	flags = fcntl(STDOUT_FILENO, F_GETFL);
	if(flags == -1)
	{
		perror("fcntl");
		exit(EXIT_FAILURE);
	}
	flags = fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);
	if(flags == -1)
	{
		perror("fcntl");
		exit(EXIT_FAILURE);
	}
	fb.fd = open(opts.filename, O_RDWR);
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
				int w;
				char *offset = mb.data + buffer_consume_at(&mb.buffer);
				int towrite = buffer_data_available(&mb.buffer);
				while((w = write(STDOUT_FILENO, offset, towrite)) == -1 && errno == EAGAIN)
				{
					towrite >>= 1;
				}
				if(w == -1)
				{
					perror("write");
					exit(EXIT_FAILURE);
				}
				mb.buffer.consumed += w;
				if(w)
				{
					pfds[0].fd = pfds[0].fd == -2 ? -2 : 0;
				}
			}
			else if(buffer_data_available(&fb.buffer))
			{
				int toread = ARRSZ(buf) < buffer_data_available(&fb.buffer) ? ARRSZ(buf) : buffer_data_available(&fb.buffer);
				int w = lseek(fb.fd, buffer_consume_at(&fb.buffer), SEEK_SET);
				int r = read(fb.fd, buf, toread);
				if(r == -1)
				{
					perror("lseek");
					exit(EXIT_FAILURE);
				}
				if(r == -1)
				{
					perror("read");
					exit(EXIT_FAILURE);
				}
				while((w = write(STDOUT_FILENO, buf, r)) == -1 && errno == EAGAIN)
				{
					r >>= 1;
				}
				if(w == -1)
				{
					perror("write");
					exit(EXIT_FAILURE);
				}
				fb.buffer.consumed += w;
				if(w)
				{
					pfds[0].fd = pfds[0].fd == -2 ? -2 : 0;
				}
			}
			else
			{
				pfds[1].fd = -1;
			}
		}
		else if(pfds[0].revents & (POLLIN | POLLHUP))
		{
			if(!buffer_space_available(&fb.buffer))
			{
				if(opts.abort)
				{
					fprintf(stderr, "File would grow beyond requested size limit\n");
					exit(EXIT_FAILURE);
				}
				pfds[0].fd = -1;
			}
			else if(!buffer_space_available(&mb.buffer) || buffer_data_available(&fb.buffer))
			{
				int toread = ARRSZ(buf) < buffer_space_available(&fb.buffer) ? ARRSZ(buf) : buffer_space_available(&fb.buffer);
				int r = read(STDIN_FILENO, buf, toread);
				int w = lseek(fb.fd, buffer_produce_at(&fb.buffer), SEEK_SET);
				if(r == -1)
				{
					perror("read");
					exit(EXIT_FAILURE);
				}
				if(r == 0)
				{
					pfds[0].fd = -2;
					close(STDIN_FILENO);
				}
				if(w == -1)
				{
					perror("lseek");
					exit(EXIT_FAILURE);
				}
				w = 0;
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
				if(w)
				{
					pfds[1].fd = 1;
				}
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
					pfds[0].fd = -2;
					close(STDIN_FILENO);
				}
				mb.buffer.produced += r;
				if(r)
				{
					pfds[1].fd = 1;
				}
			}
		}
		else
		{
			exit(EXIT_FAILURE);
		}
	} while(pfds[0].fd >= 0 || pfds[1].fd >= 0);
	exit(EXIT_SUCCESS);
}
