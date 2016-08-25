#ifndef GC_HPP_INCLUDED
#define GC_HPP_INCLUDED

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>

#include "atomic_list.hpp"
#include "color.hpp"
#include "impl_details.hpp"
#include "large_block_list.hpp"
#include "marker.hpp"
#include "mutator.hpp"
#include "phase.hpp"
#include "stub_list.hpp"

namespace otf_gc
{
  class gc
  {
  private:
    std::atomic<list<void*>> allocation_dump;

    std::atomic<stub_list*> small_used_lists[impl_details::small_size_classes];
    std::atomic<large_block_list*> large_used_list;

    std::atomic<stub_list*> small_free_lists[impl_details::small_size_classes];
    std::atomic<large_block_list*> large_free_list;

    std::atomic<bool> running;
    std::atomic<color> alloc_color;
    std::atomic<phase> gc_phase;
    std::atomic<unsigned> active, shook;

    std::mutex reg_mut;

    friend class mutator;
  public:
    static std::unique_ptr<gc> collector;
  private:
    std::atomic<list<void*>> root_set;
    atomic_list<list<void*>> buffer_set;

    bool try_advance()
    {
      if(shook.load(std::memory_order_acquire) == active.load(std::memory_order_acquire))
      {
	std::lock_guard<std::mutex> lk(reg_mut);

	if(shook.load(std::memory_order_acquire) == active.load(std::memory_order_acquire))
	{
	  shook.store(0, std::memory_order_release);
	  phase p(gc_phase.load(std::memory_order_acquire));
   	    
	  if(p == phase(phase::phase_t::Second_h))
	  {
	    color prev_color(alloc_color.load(std::memory_order_acquire));
	    alloc_color.store(prev_color.flip(), std::memory_order_release);
	  }
	  
	  gc_phase.store(p.advance(), std::memory_order_release);

	  return true;
	}
      }

      return false;
    }

  public:
    class registered_mutator : public mutator
    {
    private:
      friend class gc;

      gc& parent;
      bool inactive, snoop, trace_on;
      std::function<list<void*>()> root_callback;
      phase current_phase;
      list<void*> buffer, snooped;
    public:
      registered_mutator(gc& parent_)
	: mutator(parent_.alloc_color.load(std::memory_order_relaxed))
	, parent(parent_)
	, inactive(false)
	, snoop(parent.gc_phase.load(std::memory_order_acquire).snooping())
	, trace_on(parent.gc_phase.load(std::memory_order_acquire).tracing())
	, root_callback([]() { return nullptr; })
	, current_phase(parent.gc_phase.load(std::memory_order_acquire))
      {
	parent.active.fetch_add(1, std::memory_order_relaxed);
	parent.shook.fetch_add(1, std::memory_order_relaxed);
      }

      inline bool tracing() const {
	return trace_on;
      }

      inline bool snooping() const {
	return snoop;
      }

      inline phase mut_phase() {
	return current_phase;
      }
      
      inline void append_front_buffer(list<void*>&& buf) {
	buffer = buf.append(std::move(buffer));
      }

      inline void push_front_buffer(void* p) {
	buffer.push_front(p);
      }

      inline void push_front_snooping(void* p) {
	snooped.push_front(p);
      }

      inline void* buffer_ptr() {
	if(buffer.empty())
	  return nullptr;
	else
	  return buffer.head_ptr();
      }
      
      inline color mut_color() const {
	return alloc_color;
      }
      
      inline void set_root_callback(std::function<list<void*>()> root_callback_)
      {
	root_callback = root_callback_;
      }

      inline void poll_for_sync()
      {
	assert(!inactive);
	phase gc_phase = parent.gc_phase.load(std::memory_order_acquire);

	if(current_phase != gc_phase)
	{
	  current_phase = gc_phase;

	  if(current_phase == phase(phase::phase_t::Third_h)) {
	    list<void*> roots = root_callback();
	    
	    roots.append(std::move(snooped));
	    roots.atomic_vacate_and_append(parent.root_set);
	    
	    for(size_t i = 0; i < impl_details::small_size_classes; ++i)
	      if(auto small_ul = vacate_small_used_list(i).release())
		small_ul->atomic_vacate_and_append(parent.small_used_lists[i]);

	    if(auto large_ul = vacate_large_used_list().release())
	      large_ul->atomic_vacate_and_append(parent.large_used_list);
	    
	    alloc_color = parent.alloc_color.load(std::memory_order_acquire);
	  } else if(current_phase == phase(phase::phase_t::Fourth_h)) {
	    parent.buffer_set.push_front(buffer);
	    buffer.reset();
	  }

	  snoop = current_phase.snooping();
	  trace_on = current_phase.tracing();

	  parent.shook.fetch_add(1, std::memory_order_relaxed);
	}
      }

      ~registered_mutator()
      {
	{
	  std::lock_guard<std::mutex> lk(parent.reg_mut);

	  parent.active.fetch_sub(!inactive, std::memory_order_relaxed);

	  if(!inactive && current_phase == parent.gc_phase.load(std::memory_order_relaxed))
	    parent.shook.fetch_sub(1, std::memory_order_relaxed);
	}

	parent.buffer_set.push_front(buffer);
	
	for(size_t i = 0; i < impl_details::small_size_classes; ++i) {
	  if(auto fl = fixed_managers[i].release_free_list()) {
	    fl->atomic_vacate_and_append(parent.small_free_lists[i]);
	    delete fl;
	  }

	  if(auto ul = fixed_managers[i].release_used_list()) {
	    ul->atomic_vacate_and_append(parent.small_used_lists[i]);
	    delete ul;
	  }
	}

	if(auto lfl = variable_manager.release_free_list()) {
	  lfl->atomic_vacate_and_append(parent.large_free_list);
	  delete lfl;
	}

	if(auto lul = variable_manager.release_used_list()) {
	  lul->atomic_vacate_and_append(parent.large_used_list);
	  delete lul;
	}

	allocation_dump.atomic_vacate_and_append(parent.allocation_dump);
      }
    };
  private:
    gc()
      : large_used_list(nullptr)
      , large_free_list(nullptr)
      , running(false)
      , active(0)
      , shook(0)
    {
      for(auto& small_ul : small_used_lists)
	small_ul.store(nullptr, std::memory_order_relaxed);

      for(auto& small_fl : small_free_lists)
	small_fl.store(nullptr, std::memory_order_relaxed);
    }

    impl_details::underlying_header_t header(void* p)
    {
      impl_details::header_t& h = *reinterpret_cast<impl_details::header_t*>(p);
      return h.load(std::memory_order_relaxed);
    }
  public:
    ~gc()
    {
      while(active.load(std::memory_order_acquire) > 0);

      for(size_t i = 0; i < impl_details::small_size_classes; ++i)
	delete small_free_lists[i].exchange(nullptr, std::memory_order_relaxed);

      delete large_free_list.exchange(nullptr, std::memory_order_relaxed);

      for(size_t i = 0; i < impl_details::small_size_classes; ++i)
	delete small_used_lists[i].exchange(nullptr, std::memory_order_relaxed);

      delete large_used_list.exchange(nullptr, std::memory_order_relaxed);

      list<void*> records = allocation_dump.exchange(nullptr, std::memory_order_relaxed);

      while(!records.empty()) {
	free(records.front());
	records.pop_front();
      }

      root_set.exchange(nullptr, std::memory_order_relaxed).clear();
      buffer_set.clear();
    }

    static void initialize()
    {
      if(collector == nullptr)
	collector = std::unique_ptr<gc>(new gc());
    }

    inline std::unique_ptr<registered_mutator> create_mutator()
    {
      std::lock_guard<std::mutex> lk(reg_mut);
      std::unique_ptr<registered_mutator> mt(std::make_unique<registered_mutator>(*this));

      return mt;
    }

    inline void stop()
    {
      running.store(false, std::memory_order_acq_rel);
    }

    inline static std::unique_ptr<registered_mutator> get_mutator()
    {
      return collector->create_mutator();
    }

    template <class Tracer>
    inline void clear_buffers()
    {
      using namespace impl_details;

      list<list<void*>> lists(buffer_set.vacate());

      while(!lists.empty())
      {
	list<void*> buf(lists.front());
	lists.pop_front();
	  
	while(!buf.empty()) {
	  void* root = buf.front();
	  buf.pop_front();
	    
	  if(!root) continue;
	    
	  auto rp = reinterpret_cast<std::ptrdiff_t>(root);
	  
	  if(rp & 1ULL)
	  {	  
	    rp = rp ^ 1ULL;
		
	    header_t* header_w = reinterpret_cast<header_t*>(rp - header_size);
	    underlying_header_t header_c = header_w->load(std::memory_order_relaxed);
	      
	    size_t num_log_ptrs = Tracer::num_log_ptrs(header_c);	  
	      
	    for(std::size_t p = rp - header_size - num_log_ptrs * log_ptr_size;
		p < rp - header_size;
		p += log_ptr_size)
	      reinterpret_cast<log_ptr_t*>(p)->store(nullptr);
	  }
	}
      }
    }
    
    template <class Policy>
    void sweep(color free_color)
    {
      using namespace impl_details;
      
      static size_t ticks = 0;
      
      stub_list* remaining_free = new stub_list();
      stub_list* processed_used = new stub_list();
      stub_list* remaining_used = nullptr;
      
      for(size_t i = 0; i < impl_details::small_size_classes; ++i)
      {
      	remaining_used = small_used_lists[i].exchange(nullptr, std::memory_order_relaxed);	
      
      	while(remaining_used && !remaining_used->empty())
      	{
      	  stub* st = remaining_used->front();
      	  remaining_used->pop_front();

      	  while(!remaining_used->empty())
      	  {
      	    stub* new_st = remaining_used->front();
      	    void* offset = reinterpret_cast<void*>(reinterpret_cast<std::ptrdiff_t>(st->start) + st->size);

      	    if(offset == new_st->start) {
      	      st->size += new_st->size;
      	      remaining_used->pop_front();
      	      delete new_st;
      	    } else {
      	      break;
      	    }
      	  }

      	  for(auto p = reinterpret_cast<std::uint64_t>(st->start);
      	      p < reinterpret_cast<std::uint64_t>(st->start) + st->size;
      	      p += (1ULL << (i+3)))
      	  {
      	    underlying_header_t h = header(reinterpret_cast<void*>(p + log_ptr_size));
      	    bool free_status = color(h & header_color_mask) == free_color;

      	    if(free_status) {
      	      Policy::destroy(h, reinterpret_cast<header_t*>(p + log_ptr_size));	      
      	      reinterpret_cast<log_ptr_t*>(p)->~log_ptr_t();
      	      reinterpret_cast<header_t*>(p + log_ptr_size)->~header_t();
      	    }

      	    size_t coalesced = 1ULL << (i+3);

      	    if(free_status) {
      	      for(p += (1ULL << (i+3));
      		  p < reinterpret_cast<std::uint64_t>(st->start) + st->size;
      		  p += (1ULL << (i+3)))
      	      {
      		if(++ticks % tick_frequency == 0) {
      		  remaining_free->atomic_vacate_and_append(small_free_lists[i]);
      		  if(!running.load(std::memory_order_relaxed)) return;
      		}

      		h = header(reinterpret_cast<void*>(p + log_ptr_size));

      		if(color(h & header_color_mask) == free_color)
      		{
      		  Policy::destroy(h, reinterpret_cast<header_t*>(p + log_ptr_size));
      		  reinterpret_cast<log_ptr_t*>(p)->~log_ptr_t();
      		  reinterpret_cast<header_t*>(p + log_ptr_size)->~header_t();

      		  coalesced += 1ULL << (i+3);
      		} else {
      		  break;
      		}
      	      }
      	    } else {
      	      for(p += (1ULL << (i+3));
      		  p < reinterpret_cast<std::uint64_t>(st->start) + st->size;
      		  p += (1ULL << (i+3)))
      	      {
      		if(++ticks % tick_frequency == 0) {
      		  remaining_free->atomic_vacate_and_append(small_free_lists[i]);
      		  if(!running.load(std::memory_order_relaxed)) return;
      		}

      		h = header(reinterpret_cast<void*>(p + log_ptr_size));

      		if(color(h & header_color_mask) != free_color) {
      		  coalesced += 1ULL << (i+3);
      		} else {
      		  break;
      		}
      	      }
      	    }

      	    if(coalesced < st->size)
      	    {
      	      stub* new_stub = new stub(reinterpret_cast<void*>(p), st->size - coalesced);
      	      st->size = coalesced;

      	      free_status ? remaining_free->push_front(st) : processed_used->push_front(st);
      	      remaining_used->push_front(new_stub);
      	      break;
      	    } else {
      	      assert(p == reinterpret_cast<underlying_header_t>(st->start) + st->size - (1ULL << (i+3)));
      	      free_status ? remaining_free->push_front(st) : processed_used->push_front(st);
      	    }
      	  }
      	}

      	if(remaining_used)
      	  delete remaining_used;
      
      	processed_used->atomic_vacate_and_append(small_used_lists[i]);
      	remaining_free->atomic_vacate_and_append(small_free_lists[i]);
      }

      delete processed_used;	
      delete remaining_free;

      large_block_list* remaining_large_used = large_used_list.exchange(nullptr, std::memory_order_relaxed);

      if(!remaining_large_used) return;

      large_block_list* processed_large_free = new large_block_list();
      large_block_list* processed_large_used = new large_block_list();

      while(!remaining_large_used->empty())
      {
	using namespace impl_details;

	void* fr = remaining_large_used->front();
	remaining_large_used->pop_front();

	block_cursor blk_c(fr);
	blk_c.recalculate();

	underlying_header_t h = blk_c.header()->load(std::memory_order_relaxed);
	bool free_status = color(h & header_color_mask) == free_color;

	if(free_status)
	{
	  Policy::destroy(h, blk_c.header());

	  while((blk_c.split() & split_mask) > 0)
	  {
	    bool switch_bit = (blk_c.split() >> split_bits) & 1ULL;
	    
	    block_cursor buddy_c(switch_bit ? blk_c.start() - (1ULL << blk_c.size())
				            : blk_c.start() + (1ULL << blk_c.size()));
	    
	    buddy_c.recalculate();
	    
	    if(buddy_c.size() == blk_c.size()) {
	      h = buddy_c.header()->load(std::memory_order_relaxed);

	      if(color(h & header_color_mask) == free_color)
	      {
		buddy_c.unlink(remaining_large_used->front(), remaining_large_used->back());

		Policy::destroy(h, buddy_c.header());

		if(blk_c.start() > buddy_c.start())		  
		  std::swap(blk_c, buddy_c);
		
		for(size_t i = 0; i < buddy_c.num_log_ptrs(); ++i)
		  buddy_c.log_ptr(i)->~log_ptr_t();
		
		buddy_c.header()->~header_t();

		blk_c.split() = ((blk_c.split() & split_mask) - 1)
		              | (((blk_c.split() & split_switch_mask) >> 1ULL) & split_switch_mask);
		++blk_c.size();
	      } else {
		break;
	      }
	    } else {
	      break;
	    }
	  }
	  
	  blk_c.header()->store(zeroed_header, std::memory_order_relaxed);	  
	  	  
	  processed_large_free->push_front(blk_c);
	} else {
	  processed_large_used->push_front(blk_c);
	}

	if(++ticks % tick_frequency == 0) {
	  processed_large_free->atomic_vacate_and_append(large_free_list);
	  if(!running.load(std::memory_order_relaxed)) return;
	}
      }

      processed_large_free->atomic_vacate_and_append(large_free_list);
      processed_large_used->atomic_vacate_and_append(large_used_list);

      delete processed_large_free;
      delete remaining_large_used;
      delete processed_large_used;
    }

    template <class Policy, class Tracer>
    inline void run()
    {
      running.store(true, std::memory_order_relaxed);

      while(running.load(std::memory_order_relaxed))
      {
	assert(shook.load(std::memory_order_acquire) <= active.load(std::memory_order_acquire));
	
	if(try_advance())
	{
	  phase::phase_t p = gc_phase.load(std::memory_order_acquire);

	  switch(p)
	  {
	  case phase::phase_t::Tracing:
	    {
	      list<void*> r = root_set.exchange(nullptr, std::memory_order_relaxed);

	      marker<Tracer> m(std::move(r), running);
	      m.mark(alloc_color.load(std::memory_order_relaxed));

	      break;
	    }
	  case phase::phase_t::Sweep:
	    sweep<Policy>(alloc_color.load(std::memory_order_relaxed).flip());
	    break;

	  case phase::phase_t::Clearing:
	    clear_buffers<Tracer>();
	    break;
	    
	  default:
	    break;
	  }
	}
      }
    }
  };
}
#endif
