#ifndef FRG_TUPLE_HPP
#define FRG_TUPLE_HPP

#include <utility>

#include <frg/macros.hpp> 

namespace frg FRG_VISIBILITY {

namespace _tuple {
	template<typename... Types>
	struct storage;

	template<typename T, typename... Types>
	struct storage<T, Types...> {
		storage() = default;

		storage(T item, Types... tail)
		: item(std::move(item)), tail(std::move(tail)...) { }

		T item;
		storage<Types...> tail;
	};

	template<>
	struct storage<> {

	};

	template<int n, typename... Types>
	struct nth_type;

	template<int n, typename T, typename... Types>
	struct nth_type<n, T, Types...> {
		typedef typename nth_type<n - 1, Types...>::type  type;
	};

	template<typename T, typename... Types>
	struct nth_type<0, T, Types...> {
		typedef T type;
	};

	template<int n, typename... Types>
	struct access_helper;

	template<int n, typename T, typename... Types>
	struct access_helper<n, T, Types...> {
		static typename nth_type<n - 1, Types...>::type &access(storage<T, Types...> &stor) {
			return access_helper<n - 1, Types...>::access(stor.tail);
		}
	};

	template<typename T, typename... Types>
	struct access_helper<0, T, Types...> {
		static T &access(storage<T, Types...> &stor) {
			return stor.item;
		}
	};
} // namespace _tuple

template<typename... Types>
class tuple {
public:
	tuple() = default;

	explicit tuple(Types... args)
	: _stor(std::move(args)...) { }

	template<int n>
	typename _tuple::nth_type<n, Types...>::type &get() {
		return _tuple::access_helper<n, Types...>::access(_stor);
	}

private:
	_tuple::storage<Types...> _stor;
};

// Specialization to allow empty tuples.
template<>
class tuple<> { };

template<typename... Types>
tuple<typename std::remove_reference_t<Types>...> make_tuple(Types &&... args) {
	return tuple<typename std::remove_reference_t<Types>...>(std::forward<Types>(args)...);
}

namespace _tuple {
	template<typename F, typename... Args, size_t... I>
	auto apply(F functor, tuple<Args...> args, std::index_sequence<I...>) {
		return functor(std::move(args.template get<I>())...);
	}
} // namespace tuple

template<typename F, typename... Args>
auto apply(F functor, tuple<Args...> args) {
	return _tuple::apply(std::move(functor), std::move(args), std::index_sequence_for<Args...>());
}


} // namespace frg

#endif // FRG_TUPLE_HPP