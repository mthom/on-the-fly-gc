#ifndef WRITE_BARRIER_HPP_INCLUDED
#define WRITE_BARRIER_HPP_INCLUDED

#include <atomic>
#include <memory>

#include "color.hpp"
#include "impl_details.hpp"
#include "gc.hpp"

namespace otf_gc
{
  template <std::unique_ptr<gc::registered_mutator>&(*Alloc)(), class Tracer, class T>
  class otf_write_barrier_prelude
  {
  protected:
    void prelude(void* parent, T data)
    {
      using namespace impl_details;
      
      if(parent && Alloc()->tracing()) {
	auto hp = reinterpret_cast<std::ptrdiff_t>(parent) - header_size;
	auto h  = reinterpret_cast<header_t*>(hp)->load(std::memory_order_relaxed);
	
	if(color(h & header_color_mask) != Alloc()->mut_color())
	{
	  size_t seg_num =
	    (reinterpret_cast<std::ptrdiff_t>(&data) - reinterpret_cast<std::ptrdiff_t>(parent)) / segment_size;
	  
	  log_ptr_t* lp = Tracer::log_ptr(h, parent, seg_num);
	  assert(lp != nullptr);
	  
	  if(!lp->load()) {
	    list<void*> temp_buf = Tracer::derived_ptrs_of_obj_segment(h, parent, seg_num);
	    	    
	    if(!temp_buf.empty() && !lp->load()) {
		Alloc()->append_front_buffer(std::move(temp_buf));
		assert((reinterpret_cast<std::ptrdiff_t>(parent) & 1ULL) == 0ULL);
		Alloc()->push_front_buffer(reinterpret_cast<void*>(reinterpret_cast<std::ptrdiff_t>(parent) | 1ULL));

		lp->store(Alloc()->buffer_ptr());
	    }
	  }
	}
      }
    }
  };
  
  template <std::unique_ptr<gc::registered_mutator>&(*)(), class, class>
  class otf_write_barrier {};
  
  template <std::unique_ptr<gc::registered_mutator>&(*Alloc)(), class Tracer, class T>
  class otf_write_barrier<Alloc, Tracer, T*> : public otf_write_barrier_prelude<Alloc, Tracer, T*>
  {
  private:
    T* data;

  public:
    template <typename... Ts>
    otf_write_barrier(Ts&&... items) : data(std::forward<Ts>(items)...)
    {}

    otf_write_barrier(T* data_ = nullptr) : data(data_) {}
    
    inline operator T*() {
      return data;
    }
    
    inline operator void*() {
      return reinterpret_cast<void*>(data);
    }

    otf_write_barrier<Alloc, Tracer, T*>& operator=(T*) = delete;
  
    inline void write(void* parent, T* data_)
    {
      this->prelude(parent, data);
      
      data = data_;
      if(Alloc()->snooping() && data)
	Alloc()->push_front_snooping(data->derived_ptr());
    }
  
    inline T* get() {
      return data;
    }
  
    inline T* const operator->() const
    {
      return data;
    }

    inline bool operator==(const otf_write_barrier<Alloc, Tracer, T*>& it) const
    {
      return data == it.data;
    }

    inline bool operator!=(const otf_write_barrier<Alloc, Tracer, T*>& it) const
    {
      return data != it.data;
    }
  };

  template <std::unique_ptr<gc::registered_mutator>&(*Alloc)(), class Tracer, class T>
  class otf_write_barrier<Alloc, Tracer, std::atomic<T*>> : public otf_write_barrier_prelude<Alloc, Tracer, std::atomic<T*>&>
  {
  private:
    std::atomic<T*> data;

  public:
    otf_write_barrier(T* data_) : data(data_) {}

    inline bool operator==(T* const t) const
    {
      return data == t;
    }

    inline T* load(std::memory_order mem) const
    {
      return data.load(mem);
    }

    inline void store(void* parent, T* val, std::memory_order mem)
    {
      this->prelude(parent, data);

      data.store(val, mem);
      if(Alloc()->snooping() && val) 
	Alloc()->push_front_snooping(val->derived_ptr());      
    }

    inline bool compare_exchange_strong(void* parent,
					T*& expected,
					T* desired,
					std::memory_order success,
					std::memory_order failure)
    {
      T* old_expected = expected;

      this->prelude(parent, data);
      
      bool result = data.compare_exchange_strong(old_expected, desired, success, failure);
      
      if(result && desired && Alloc()->snooping())
	Alloc()->push_front_snooping(desired->derived_ptr());
    
      return result;
    }
  };
}
#endif
