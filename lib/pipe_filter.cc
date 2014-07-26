/* -*- c++ -*- */
/*
 * Copyright 2004,2010 Free Software Foundation, Inc.
 * 
 * This file is part of GNU Radio
 * 
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for features, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include <iostream>
#include <pipe/filter.h>
#include <gnuradio/io_signature.h>

/*
 * Create a new instance of pipe_filter and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
pipe_filter_sptr 
pipe_make_filter (size_t in_item_sz,
                  size_t out_item_sz,
                  double relative_rate,
                  const char *cmd)
{
  return gnuradio::get_initial_sptr(new pipe_filter (in_item_sz,
                                                     out_item_sz,
                                                     relative_rate,
                                                     cmd));
}

/*
 * Specify constraints on number of input and output streams.
 * This info is used to construct the input and output signatures
 * (2nd & 3rd args to gr_block's constructor).  The input and
 * output signatures are used by the runtime system to
 * check that a valid number and type of inputs and outputs
 * are connected to this block.
 */
static const int MIN_IN = 1;	// mininum number of input streams
static const int MAX_IN = 1;	// maximum number of input streams
static const int MIN_OUT = 1;	// minimum number of output streams
static const int MAX_OUT = 1;	// maximum number of output streams

/*
 * The private constructor
 */

pipe_filter::pipe_filter (size_t in_item_sz,
                          size_t out_item_sz,
                          double relative_rate,
                          const char *cmd)
  : gr::block ("pipe_filter",
	      gr::io_signature::make (MIN_IN,  MAX_IN,  in_item_sz),
	      gr::io_signature::make (MIN_OUT, MAX_OUT, out_item_sz)),
    d_in_item_sz (in_item_sz),
    d_out_item_sz (out_item_sz),
    d_relative_rate (relative_rate)
{
  set_relative_rate(d_relative_rate);

  create_command_process(cmd);
}

/*
 * Our virtual destructor.
 */
pipe_filter::~pipe_filter ()
{
  long ret;
  char buf[PIPE_BUF];
  ssize_t sz;
  int i;
  int pstat;

  // Set file descriptors to blocking, to be sure to consume
  // the remaining output generated by the process.
  reset_fd_flags(d_cmd_stdin_pipe[1], O_NONBLOCK);
  reset_fd_flags(d_cmd_stdout_pipe[0], O_NONBLOCK);

  fclose(d_cmd_stdin);

  i = 0;
  do {
    sz = read(d_cmd_stdout_pipe[0], buf, sizeof (buf));
    if (sz < 0) {
      perror("read()");
      break ;
    }
    i++;
  } while (i < 256 && sz > 0);
  fclose(d_cmd_stdout);

  do {
    ret = waitpid(d_cmd_pid, &pstat, 0);
  } while (ret == -1 && errno == EINTR);

  if (ret == -1) {
    perror("waitpid()");
    return ;
  }

  if (WIFEXITED(pstat))
    std::cerr << "Process exited with code " << WEXITSTATUS(pstat) << std::endl;
  else
    std::cerr << "Abnormal process termination" << std::endl;
}

void
pipe_filter::set_unbuffered(bool unbuffered)
{
  d_unbuffered = unbuffered;
}

void
pipe_filter::set_fd_flags(int fd, long flags)
{
  long ret;

  ret = fcntl(fd, F_GETFL);
  if (ret == -1) {
    perror("fcntl()");
    throw std::runtime_error("fcntl() error");
  }

  ret = fcntl(fd, F_SETFL, ret | flags);
  if (ret == -1) {
    perror("fcntl()");
    throw std::runtime_error("fcntl() error");
  }
}

void
pipe_filter::reset_fd_flags(int fd, long flag)
{
  long ret;

  ret = fcntl(fd, F_GETFL);
  if (ret == -1) {
    perror("fcntl()");
    throw std::runtime_error("fcntl() error");
  }

  ret = fcntl(fd, F_SETFL, ret & ~flag);
  if (ret == -1) {
    perror("fcntl()");
    throw std::runtime_error("fcntl() error");
  }
}

void
pipe_filter::create_pipe(int pipefd[2])
{
  int ret;

  ret = pipe(pipefd);
  if (ret != 0) {
    perror("pipe()");
    throw std::runtime_error("pipe() error");
  }
}

void
pipe_filter::create_command_process(const char *cmd)
{
  create_pipe(d_cmd_stdin_pipe);
  create_pipe(d_cmd_stdout_pipe);

  d_cmd_pid = fork();
  if (d_cmd_pid == -1) {
    perror("fork()");
    return ;
  }
  else if (d_cmd_pid == 0) {
    dup2(d_cmd_stdin_pipe[0], STDIN_FILENO);
    close(d_cmd_stdin_pipe[0]);
    close(d_cmd_stdin_pipe[1]);

    dup2(d_cmd_stdout_pipe[1], STDOUT_FILENO);
    close(d_cmd_stdout_pipe[1]);
    close(d_cmd_stdout_pipe[0]);

    execl("/bin/sh", "sh", "-c", cmd, NULL);

    perror("execl()");
    exit(EXIT_FAILURE);
  }
  else {
    close(d_cmd_stdin_pipe[0]);
    set_fd_flags(d_cmd_stdin_pipe[1], O_NONBLOCK);
    fcntl(d_cmd_stdin_pipe[1], F_SETFD, FD_CLOEXEC);

    set_fd_flags(d_cmd_stdout_pipe[0], O_NONBLOCK);
    fcntl(d_cmd_stdout_pipe[0], F_SETFD, FD_CLOEXEC);
    close(d_cmd_stdout_pipe[1]);

    d_cmd_stdin = fdopen(d_cmd_stdin_pipe[1], "w");
    if (d_cmd_stdin == NULL) {
      perror("fdopen()");
      throw std::runtime_error("fdopen() error");
      return ;
    }

    d_cmd_stdout = fdopen(d_cmd_stdout_pipe[0], "r");
    if (d_cmd_stdout == NULL) {
      perror("fdopen()");
      throw std::runtime_error("fdopen() error");
      return ;
    }
  }
}


void 
pipe_filter::forecast (int noutput_items, gr_vector_int &ninput_items_required)
{
  ninput_items_required[0] = (double)(noutput_items) / d_relative_rate;
}

int
pipe_filter::read_process_output(uint8_t *out, int nitems)
{
  size_t ret;

  ret = fread(out, d_out_item_sz, nitems, d_cmd_stdout);
  if (    ret == 0
       && ferror(d_cmd_stdout)
       && errno != EAGAIN
       && errno != EWOULDBLOCK) {
    throw std::runtime_error("fread() error");
    return (-1);
  }

  return (ret);
}

int
pipe_filter::write_process_input(const uint8_t *in, int nitems)
{
  size_t ret;

  ret = fwrite(in, d_in_item_sz, nitems, d_cmd_stdin);
  if (    ret == 0
       && ferror(d_cmd_stdin)
       && errno != EAGAIN
       && errno != EWOULDBLOCK) {
    throw std::runtime_error("fwrite() error");
    return (-1);
  }

  if (d_unbuffered)
    fflush(d_cmd_stdin);

  return (ret);
}


int 
pipe_filter::general_work (int noutput_items,
                           gr_vector_int &ninput_items,
                           gr_vector_const_void_star &input_items,
                           gr_vector_void_star &output_items)
{
  const uint8_t *in = (const uint8_t *) input_items[0];
  int n_in_items = ninput_items[0];
  uint8_t *out = (uint8_t *) output_items[0];
  int n_produced;
  int n_consumed;

  n_produced = read_process_output(out, noutput_items);
  if (n_produced < 0)
    return (n_produced);

  n_consumed = write_process_input(in, n_in_items);
  if (n_consumed < 0)
    return (n_consumed);

  consume_each(n_consumed);

  return (n_produced);
}
