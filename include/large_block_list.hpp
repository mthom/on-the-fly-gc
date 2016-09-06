#ifndef LARGE_BLOCK_LIST_HPP_INCLUDED
#define LARGE_BLOCK_LIST_HPP_INCLUDED

#include <atomic>
#include <cassert>

#include "impl_details.hpp"

namespace otf_gc
{
  using namespace impl_details;

  struct block_cursor
  {
    std::ptrdiff_t size_d, split_d, prev_d, next_d, log_ptr_d, header_d;

    block_cursor(void* blk, size_t num_log_ptrs = 0)
      : size_d(reinterpret_cast<std::ptrdiff_t>(blk))
      , split_d(size_d + sizeof(size_t))
      , prev_d(split_d + sizeof(size_t))
      , next_d(prev_d + sizeof(void*))
      , log_ptr_d(next_d + sizeof(void*))
      , header_d(log_ptr_d + sizeof(size_t) + log_ptr_size * num_log_ptrs)
    {}

    block_cursor(std::ptrdiff_t blk, size_t num_log_ptrs = 0)
      : size_d(blk)
      , split_d(blk + sizeof(size_t))
      , prev_d(split_d + sizeof(size_t))
      , next_d(prev_d + sizeof(void*))
      , log_ptr_d(next_d + sizeof(void*))
      , header_d(log_ptr_d + sizeof(size_t) + log_ptr_size * num_log_ptrs)
    {}

    inline std::ptrdiff_t start() const
    {
      return size_d;
    }

    inline void** prev()
    {
      return reinterpret_cast<void**>(prev_d);
    }

    inline void** next()
    {
      return reinterpret_cast<void**>(next_d);
    }

    inline size_t& size()
    {
      return *reinterpret_cast<size_t*>(size_d);
    }

    inline size_t& split()
    {
      return *reinterpret_cast<size_t*>(split_d);
    }

    inline std::ptrdiff_t data()
    {
      return start() + large_block_metadata_size + log_ptr_size * num_log_ptrs();
    }

    inline header_t* header()
    {
      return reinterpret_cast<header_t*>(header_d);
    }

    inline size_t& num_log_ptrs()
    {
      return *reinterpret_cast<size_t*>(log_ptr_d);
    }

    inline void recalculate()
    {
      header_d = num_log_ptrs() * log_ptr_size + log_ptr_d + sizeof(size_t);
    }

    inline log_ptr_t* log_ptr(size_t i)
    {
      return reinterpret_cast<log_ptr_t*>(log_ptr_d + sizeof(size_t) + i * log_ptr_size);
    }

    inline bool null_block() const
    {
      return size_d == 0;
    }

    inline void write(size_t sz, size_t spl, void* prev_, void* next_)
    {
      size() = sz;
      split() = spl;
      *next() = next_;
      *prev() = prev_;
    }

    block_cursor& operator=(std::ptrdiff_t) = delete;

    inline block_cursor& operator=(void* blk)
    {
      size_d = reinterpret_cast<std::ptrdiff_t>(blk);
      split_d = size_d + sizeof(size_t);
      prev_d = split_d + sizeof(size_t);
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

    inline void split(block_cursor& blk_c, size_t sz)
    {
      using namespace impl_details;
      
      size_t blk_sz  = blk_c.size();
      size_t blk_spl = blk_c.split() & split_mask;
      size_t blk_spl_key = (blk_c.split() & split_switch_mask) >> split_switch_bits;

      assert(sz >= 10);
      assert(blk_sz >= sz);

      void* next = *blk_c.next();

      if(blk_sz > sz) {
	if(tail == reinterpret_cast<void*>(blk_c.start()))
	  tail = reinterpret_cast<void*>(blk_c.start() + (1ULL << (blk_sz - 1)));

	if(next) {
	  block_cursor next_c(next);
	  *next_c.prev() = reinterpret_cast<void*>(blk_c.start() + (1ULL << (blk_sz - 1)));
	}
      }

      for(size_t i = blk_sz - 1; i >= sz; --i) {
	block_cursor new_blk_c(blk_c.start() + (1ULL << i));

	blk_spl_key <<= 1;
	
	new_blk_c.write(i,
			(blk_spl + blk_sz - i) | ((blk_spl_key | 1ULL) << split_bits),
			i == sz
			? reinterpret_cast<void*>(blk_c.start())
			: reinterpret_cast<void*>(blk_c.start() + (1ULL << (i - 1))),
			next);

	new_blk_c.num_log_ptrs() = 0;
	new_blk_c.recalculate();

	new(new_blk_c.header()) header_t(zeroed_header);

	next = reinterpret_cast<void*>(new_blk_c.start());
      }

      blk_c.split() = (blk_spl + blk_sz - sz) | (blk_spl_key << split_bits);
      *blk_c.next() = next;
      blk_c.size()  = sz;

      blk_c.unlink(head, tail);
    }

  public:
    large_block_list(void* head_ = nullptr, void* tail_ = nullptr) noexcept
      : head(head_)
      , tail(tail_)
    {}

    inline operator bool() const {
      return !empty();
    }
    
    void clear()
    {
      block_cursor blk_c(head);

      while(!blk_c.null_block()) {
	blk_c.recalculate();

	for(size_t i = 0; i < blk_c.num_log_ptrs(); ++i)
	  blk_c.log_ptr(i)->~log_ptr_t();
	
	blk_c.header()->~header_t();
	blk_c = *blk_c.next();
      }
    }

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
      push_front(reinterpret_cast<void*>(blk.start()), blk.size(), blk.split());
    }

    inline void push_back(void* blk, size_t sz, size_t split = 0)
    {
      assert(blk != nullptr);
      assert(sz >= 10);

      block_cursor blk_c(blk), tail_c(tail);
      blk_c.write(sz, split, tail, nullptr);

      if(tail)
	*tail_c.next() = blk;
      else {
	assert(!head);
	head = blk;
      }

      tail = blk;
    }

    inline void push_front(void* blk, size_t sz, size_t split = 0)
    {
      assert(blk != nullptr);
      assert(sz >= 10);

      block_cursor blk_c(blk), head_c(head);
      blk_c.write(sz, split, nullptr, head);

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

    inline void* get_block(size_t sz)
    {
      block_cursor blk_c(head);

      size_t d = 0;

      while(!blk_c.null_block()) {
	if(blk_c.size() >= sz) {
	  split(blk_c, sz);	  	  
	  return reinterpret_cast<void*>(blk_c.start());
	}

	if(++d == impl_details::search_depth)
	  break;

	blk_c = *blk_c.next();
      }

      return nullptr;
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
