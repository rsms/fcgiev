/*
 * Copyright (c) 2001-2007 Peter Simons <simons@cryp.to>
 *
 * This software is provided 'as-is', without any express or
 * implied warranty. In no event will the authors be held liable
 * for any damages arising from the use of this software.
 *
 * Copying and distribution of this file, with or without
 * modification, are permitted in any medium without royalty
 * provided the copyright notice and this notice are preserved.
 */

#ifndef _FCGIEV_POLLVECTOR_H_
#define _FCGIEV_POLLVECTOR_H_

#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>

#define MIN_SIZE 32

typedef struct pollv_t {
  pollfd* array;
  size_t  array_size;
  size_t  array_len;
} pollv_t;


pollv_t *pollv_new(size_t size) {
  
}


void pollv_reserve(pollv_t *self, size_t size) {
  size_t  new_size;
  pollfd* new_array;
  if (size <= self->array_size) 
    return;
  for ( new_size = (self->array_size > 0) ? self->array_size * 2 : MIN_SIZE;
        new_size < size;
        new_size *= 2
      );
  new_array = (pollfd *)realloc(self->array, new_size * sizeof(pollfd));
  self->array = new_array;
  self->array_size = new_size;
}


class pollvector
{
public:
  pollvector() : array(0), array_size(0), array_len(0)
  {
    reserve(1);
  }

  ~pollvector()
  {
    if (array)
      free(array);
  }

  pollfd & operator[](int fd)
  {
    reserve(array_len+1);
    pollfd tmp;
    tmp.fd = fd;
    std::pair<pollfd*,pollfd*> i = std::equal_range(array, array+array_len, tmp, pollfd_less());
    if (i.first - i.second > 1)
      throw std::logic_error("scheduler::pollvector: The internal poll array is broken!");
    else if (i.first == i.second)
    {      // We have to insert it.
      memmove(i.first+1, i.first, (array+array_len-i.first)*sizeof(pollfd));
      ++array_len;
      i.first->fd      = fd;
      i.first->events  = 0;
      i.first->revents = 0;
      return *i.first;
    }
    else
      return *i.first;  // Found it.
  }

  void erase(int fd)
  {
    pollfd tmp;
    tmp.fd = fd;
    std::pair<pollfd*,pollfd*> i = std::equal_range(array, array+array_len, tmp, pollfd_less());
    if (i.first - i.second > 1)
      throw std::logic_error("scheduler::pollvector: The interal poll array is broken!");
    else if (i.first == i.second)
      return;
    else
    {      // Found it.
      memmove(i.first, i.first+1, (array+array_len-i.first-1)*sizeof(pollfd));
      reserve(--array_len);
    }
  }

  size_t length() const
  {
    return array_len;
  }

  pollfd* get_pollfd_array()
  {
    return array;
  }

#if 0
  void dump(std::ostream& os) const
  {
    os << "The poll vector has " << array_len << " entries; size is " << array_size << "." << std::endl;
    for (size_t i = 0; i < array_len; ++i)
      os << "fd = "        << std::dec << array[i].fd << "; "
         << "events = 0x"  << std::hex << array[i].events << "; "
         << "revents = 0x" << std::hex << array[i].revents
         << std::dec << std::endl;
  }
#endif

private:
  pollvector(pollvector const &);
  pollvector & operator= (pollvector const &);

  void reserve(size_t size)
  {
    size_t  new_size;
    pollfd* new_array;

    if (size <= array_len)
      return;    // Bullshit.
#if 0
    else if (size < array_size/2 && array_size/2 > MIN_SIZE)
    {      // Shrink array.
      for (size_t n = array_size/2; n > size && n > MIN_SIZE; n /= 2)
        new_size = n;
      debug("Shrinking pollvec array from %d to %d slots; we have %d entries.", array_size, new_size, array_len);
      new_array = static_cast<pollfd*>(realloc(array, new_size*sizeof(pollfd)));
      if (new_array == 0)
        return;    // How is this supposed to happen?
      array      = new_array;
      array_size = new_size;
    }
#endif
    else if (size > array_size)
    {      // Enlarge array.
      for (new_size = (array_size > 0) ? array_size*2 : MIN_SIZE; new_size < size; new_size *= 2)
        ;
      new_array = static_cast<pollfd*>(realloc(array, new_size*sizeof(pollfd)));
      if (new_array == 0)
        throw std::bad_alloc();
      array      = new_array;
      array_size = new_size;
    }
  }

  static const size_t MIN_SIZE = 32;

  pollfd* array;
  size_t  array_size;
  size_t  array_len;

  struct pollfd_less
  {
    bool operator()(pollfd const & lhs, pollfd const & rhs) { return lhs.fd < rhs.fd; }
  };
};

#endif
