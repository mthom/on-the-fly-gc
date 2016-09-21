#ifndef MUTATOR_HPP_INCLUDED
#define MUTATOR_HPP_INCLUDED

#include <array>

#include "atomic_list.hpp"
#include "color.hpp"
#include "fixed_list_manager.hpp"
#include "variable_list_manager.hpp"

namespace otf_gc
{
  class mutator
  {
  protected:
    std::array<fixed_list_manager, impl_details::small_size_classes> fixed_managers;
    variable_list_manager variable_manager;
    list<void*> allocation_dump;
    color alloc_color;

    inline impl_details::underlying_header_t create_header(impl_details::underlying_header_t);    
    inline bool transfer_small_blocks_from_collector(size_t);
    
    void* allocate_small(size_t, impl_details::underlying_header_t);
    void* allocate_large(size_t, impl_details::underlying_header_t, size_t);

    mutator(color c)
      : fixed_managers{{fixed_list_manager(3)
	  , fixed_list_manager(4)
	  , fixed_list_manager(5)
	  , fixed_list_manager(6)
	  , fixed_list_manager(7)
	  , fixed_list_manager(8)
	  , fixed_list_manager(9)}}
      , alloc_color(c)
    {}
  public:
    virtual ~mutator() {}

    inline static size_t binary_log(int);

    void* allocate(int, impl_details::underlying_header_t, size_t);

    stub_list vacate_small_used_list(size_t);
    large_block_list vacate_large_used_list();
  };
}
#endif
