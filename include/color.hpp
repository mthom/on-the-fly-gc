#ifndef COLOR_HPP_INCLUDED
#define COLOR_HPP_INCLUDED

namespace otf_gc
{
  struct color
  {
    enum class color_t : int8_t
    {
      Blue = 0x00,
      Black = 0x01,
      White = 0x02
    };

    color_t c;

    color(int8_t h) noexcept : c(static_cast<color_t>(h))
    {
      assert(0 <= h && h <= 2);
    }
    
    color(color_t c_ = color_t::Black) noexcept
      : c(c_) {}

    inline color flip() const {
      if(c == color_t::Black)
	return color_t::White;
      else
	return color_t::Black;
    }

    inline bool operator!=(const color& co) const
    {
      return c != co.c;
    }
    
    inline bool operator==(const color& co) const
    {
      return c == co.c;
    }
    
    operator color_t() {
      return c;
    }
  };
}

#endif
