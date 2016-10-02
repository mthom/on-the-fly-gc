#ifndef PHASE_HPP_INCLUDED
#define PHASE_HPP_INCLUDED

namespace otf_gc
{
  struct phase
  {
    enum class phase_t : int8_t
    {
      First_h  = 0x00,
      Second_h,
      Third_h,
      Tracing,
      Fourth_h,
      Sweep      
    };

    phase_t p;

    phase() noexcept : p(phase_t::First_h) {}

    phase(phase_t p_) noexcept : p(p_) {}
  
    inline phase_t& advance() {
      return p = phase_t((static_cast<int8_t>(p) + 1) % 6);
    }

    inline phase_t prev() const {
      return phase_t((static_cast<int8_t>(p) - 1) % 6);
    }
    
    inline bool snooping() {
      return static_cast<int8_t>(p) <= 1;      
    }
      
    inline bool tracing() {      
      auto val = static_cast<int8_t>(p);      
      return (val >= 1) && (val <= 3);
    }

    operator phase_t() {
      return p;
    }
  
    inline bool operator==(const phase& ph) const {
      return p == ph.p;
    }
  };
}
#endif
