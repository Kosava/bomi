#ifndef TMP_HPP
#define TMP_HPP

#include <type_traits>
#include <cstdint>

namespace tmp { // simple template meta progamming
	template <int N> constexpr int log2() { static_assert(N != 0, "wrong argument for log2"); return log2<N/2>() + 1; }
	template <> constexpr int log2<1>() { return 0; }
	template <typename T> constexpr int log2bitsof() { return log2<sizeof(T)*8>(); }

	template <int bits, bool sign> struct integer { /*typedef char type;*/ };
	template <> struct integer<16, true>  { typedef std::int16_t  type; };
	template <> struct integer<32, true>  { typedef std::int32_t  type; };
	template <> struct integer<64, true>  { typedef std::int64_t  type; };
	template <> struct integer<16, false> { typedef std::uint16_t type; };
	template <> struct integer<32, false> { typedef std::uint32_t type; };
	template <> struct integer<64, false> { typedef std::uint64_t type; };

	template <int bits> struct floating_point { typedef char type; };
	template <> struct floating_point<32> { typedef float  type; };
	template <> struct floating_point<64> { typedef double type; };

	template<int idx, int size, bool go = idx < size>
	struct static_for { };

	template<int idx, int size>
	struct static_for<idx, size, true> {
		template<class... Args, class F>
		static inline void run(const std::tuple<Args...> &tuple, const F &func) {
			func(std::get<idx>(tuple));
			static_for<idx+1, size>::run(tuple, func);
		}
	};

	template<int idx, int size>
	struct static_for<idx, size, false> {
		template<class T, class F>
		static inline void run(const T &, const F &) { }
		template<class T, class F>
		static inline void run(T &&, const F &) { }
	};
}


#endif // TMP_HPP
