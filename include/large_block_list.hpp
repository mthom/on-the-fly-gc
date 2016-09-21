#ifndef LARGE_BLOCK_LIST_HPP_INCLUDED
#define LARGE_BLOCK_LIST_HPP_INCLUDED

#include <atomic>
#include <cassert>
#include <new>

#include "impl_details.hpp"

namespace otf_gc
{
  using namespace impl_details;

  struct block_cursor
  {
    std::ptrdiff_t prev_d, next_d, log_ptr_d, header_d;

    block_cursor(void* blk, std::size_t num_log_ptrs = 0)
      : prev_d(reinterpret_cast<std::ptrdiff_t>(blk))
      , next_d(prev_d + sizeof(void*))
      , log_ptr_d(next_d + sizeof(void*))
      , header_d(log_ptr_d + sizeof(std::size_t) + log_ptr_size * num_log_ptrs)
    {}

    block_cursor(std::ptrdiff_t blk, std::size_t num_log_ptrs = 0)
      : prev_d(blk)
      , next_d(prev_d + sizeof(void*))
      , log_ptr_d(next_d + sizeof(void*))
      , header_d(log_ptr_d + sizeof(std::size_t) + log_ptr_size * num_log_ptrs)
    {}

    inline std::ptrdiff_t start() const
    {
      return prev_d;
    }

    inline void** prev()
    {
      return reinterpret_cast<void**>(prev_d);
    }

    inline void** next()
    {
      return reinterpret_cast<void**>(next_d);
    }

    inline std::ptrdiff_t data()
    {
      return start() + large_block_metadata_size + log_ptr_size * num_log_ptrs();
    }

    inline header_t* header()
    {
      return reinterpret_cast<header_t*>(header_d);
    }

    inline std::size_t& num_log_ptrs()
    {
      return *reinterpret_cast<std::size_t*>(log_ptr_d);
    }

    inline void recalculate()
    {
      header_d = num_log_ptrs() * log_ptr_size + log_ptr_d + sizeof(std::size_t);
    }

    inline log_ptr_t* log_ptr(std::size_t i)
    {
      return reinterpret_cast<log_ptr_t*>(log_ptr_d + sizeof(std::size_t) + i * log_ptr_size);
    }

    inline bool null_block() const
    {
      return prev_d == 0;
    }

    inline void write(void* prev_, void* next_)
    {
      *next() = next_;
      *prev() = prev_;
    }

    block_cursor& operator=(std::ptrdiff_t) = delete;

    inline block_cursor& operator=(void* blk)
    {
      prev_d = reinterpret_cast<std::ptrdiff_t>(blk);
      next_d = prev_d + sizeof(void*);
      log_ptr_d = next_d + sizeof(void*);

      if(blk)
	recalculate();

      return *this;
    }

    inline void unlink(void*& head, void*& tail)
    {
      block_cursor prev_c(*prev()), next_c(*next());

      if(tail == reinterpret_cast<void*>(start()))
	tail = *prev();

      if(*prev()) {
	*prev_c.next() = reinterpret_cast<void*>(next_c.start());
	*prev() = nullptr;
      }

      if(head == reinterpret_cast<void*>(start()))
	head = *next();

      if(*next()) {
	*next_c.prev() = reinterpret_cast<void*>(prev_c.start());
	*next() = nullptr;
      }
    }
  };

  class large_block_list
  {
  private:
    void* head;
    void* tail;

  public:
    large_block_list(void* head_ = nullptr, void* tail_ = nullptr) noexcept
      : head(head_)
      , tail(tail_)
    {}

    inline operator bool() const {
      return !empty();
    }
    /*    
    void clear()
    {
      block_cursor blk_c(head);

      while(!blk_c.null_block()) {
	blk_c.recalculate();

	for(std::size_t i = 0; i < blk_c.num_log_ptrs(); ++i)
	  blk_c.log_ptr(i)->~log_ptr_t();
	
	blk_c.header()->~header_t();
	blk_c = *blk_c.next();
      }
    }
    */
    inline bool empty() const
    {
      return head == nullptr;
    }

    inline void*& front()
    {
      return head;
    }

    inline void reset()
    {
      head = tail = nullptr;
    }
    
    inline void pop_front()
    {
      if(head) {
	block_cursor blk_c(head);
	head = *blk_c.next();

	*blk_c.next() = nullptr;
	blk_c = head;

	if(head)
	  *blk_c.prev() = nullptr;
	else
	  tail = nullptr;
      }
    }

    inline void*& back()
    {
      return tail;
    }

    inline void pop_back()
    {
      if(tail)
      {
	block_cursor blk_c(tail);
	tail = *blk_c.prev();

	*blk_c.prev() = nullptr;
	blk_c = tail;

	if(tail)
	  *blk_c.next() = nullptr;
	else
	  head = nullptr;
      }
    }

    inline void push_front(block_cursor blk)
    {
      push_front(reinterpret_cast<void*>(blk.start()));
    }

    inline void push_back(void* blk)
    {
      assert(blk != nullptr);

      block_cursor blk_c(blk), tail_c(tail);
      blk_c.write(tail, nullptr);

      if(tail)
	*tail_c.next() = blk;
      else {
	assert(!head);
	head = blk;
      }

      tail = blk;
    }

    inline void push_front(void* blk)
    {
      assert(blk != nullptr);

      block_cursor blk_c(blk), head_c(head);
      blk_c.write(nullptr, head);

      if(head)
	*head_c.prev() = blk;
      else {
	assert(!tail);
	tail = blk;
      }

      head = blk;
    }

    inline void append(large_block_list&& blk)
    {
      if(head) {
	block_cursor blk_c(blk.head);
	block_cursor tail_c(tail);

	if(blk.head) {
	  *tail_c.next() = blk.head;
	  *blk_c.prev()  = tail;
	}
      } else {
	head = blk.head;
      }

      tail = blk.tail;
      blk.head = blk.tail = nullptr;
    }

    inline void atomic_vacate_and_append(std::atomic<large_block_list>& lbl)
    {
      if(head == nullptr)
	return;

      large_block_list copy_lbl(head, tail);
      head = tail = nullptr;
      large_block_list atomic_lbl = lbl.exchange(nullptr, std::memory_order_relaxed);

      while(true)
      {
	if(!atomic_lbl.empty())
	  copy_lbl.append(std::move(atomic_lbl));

	copy_lbl = lbl.exchange(copy_lbl, std::memory_order_relaxed);

	if(!copy_lbl.empty()) {
	  atomic_lbl = lbl.exchange(nullptr, std::memory_order_relaxed);
	} else {
	  break;
	}
      }
    }
  };
}
#endif
