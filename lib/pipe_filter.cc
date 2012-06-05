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

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

//#include <iostream>
#include <pipe_filter.h>
#include <gr_io_signature.h>

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
  : gr_block ("pipe_filter",
	      gr_make_io_signature (MIN_IN,  MAX_IN,  in_item_sz),
	      gr_make_io_signature (MIN_OUT, MAX_OUT, out_item_sz)),
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
}

void
pipe_filter::set_nonblock_fd(int fd)
{
  long ret;

  ret = fcntl(fd, F_GETFL);
  if (ret == -1) {
    perror("fcntl()");
    throw std::runtime_error("fcntl() error");
  }

  ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);
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
    set_nonblock_fd(d_cmd_stdin_pipe[1]);

    set_nonblock_fd(d_cmd_stdout_pipe[0]);
    close(d_cmd_stdout_pipe[1]);
  }
}


void 
pipe_filter::forecast (int noutput_items, gr_vector_int &ninput_items_required)
{
  ninput_items_required[0] = (double)(noutput_items) / d_relative_rate;
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

  int n = std::min<int>(n_in_items, noutput_items);

  std::cout << "Debug: processing " << n << " samples." << std::endl;

  int ret;
  ret = read(d_cmd_stdout_pipe[0], out, noutput_items * d_out_item_sz);
  printf("read ret=%i\n", ret);
  if (ret < 0)
    perror("read()");

  ret = write(d_cmd_stdin_pipe[1], in, n_in_items * d_in_item_sz);
  printf("write ret=%i\n", ret);
  if (ret < 0)
    perror("write()");

  return n;
}
