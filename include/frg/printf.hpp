#pragma once

#include <frg/macros.hpp>
#include <frg/expected.hpp>
#include <frg/formatting.hpp>

namespace frg FRG_VISIBILITY {

union arg {
	uintmax_t i;
	long double f;
	void *p;
};

struct va_struct {
	va_list args;
	arg *arg_list;
	int num_args = 0;
};

enum class printf_size_mod {
	default_size,
	long_size,
	longlong_size,
	longdouble_size,
	native_size
};

template<typename T>
T pop_arg(va_struct *vsp, format_options *opts) {
	auto pop_va_arg = [&] {
		// TODO: implement std::make_unsigned in cxxshim.
		if constexpr (std::is_same_v<T, unsigned char> ||
				std::is_same_v<T, char> ||
				std::is_same_v<T, signed char> ||
				std::is_same_v<T, unsigned short> ||
				std::is_same_v<T, short> ||
				std::is_same_v<T, signed short>) {
			return static_cast<T>(va_arg(vsp->args, int));
		} else {
			return va_arg(vsp->args, T);
		}
	};

	auto get_union_member = [&] (int pos) {
		if constexpr (std::is_same_v<T, void*>)
			return &vsp->arg_list[pos].p;
		else if constexpr (std::is_floating_point_v<T>)
			return reinterpret_cast<T*>(&vsp->arg_list[pos].f);
		else
			return reinterpret_cast<T*>(&vsp->arg_list[pos].i);
	};

	if (opts->arg_pos == -1)
		return pop_va_arg();

	FRG_ASSERT(opts->arg_pos <= vsp->num_args);
	if (opts->arg_pos < vsp->num_args)
		return *get_union_member(opts->arg_pos);

	auto arg = pop_va_arg();
	*get_union_member(vsp->num_args++) = arg;
	return arg;
}

template<typename A>
frg::expected<format_error> printf_format(A agent, const char *s, va_struct *vsp) {
	while(*s) {
		if(*s != '%') {
			size_t n = 1;
			while(s[n] && s[n] != '%')
				n++;
			auto res = agent(s, n);
			if (!res)
				return res;
			s += n;
			continue;
		}

		++s;
		FRG_ASSERT(*s);

		if(*s == '%') {
			auto res = agent('%');
			if (!res)
				return res;
			++s;
			continue;
		}


		format_options opts;
		while(true) {
			if (*s >= '0' && *s <= '9' && s[1] && s[1] == '$') {
				opts.arg_pos = *s - '0';
				s += 2;
				FRG_ASSERT(*s);
			} else if(*s == '-') {
				opts.left_justify = true;
				++s;
				FRG_ASSERT(*s);
			}else if(*s == '+') {
				opts.always_sign = true;
				++s;
				FRG_ASSERT(*s);
			}else if(*s == ' ') {
				opts.plus_becomes_space = true;
				++s;
				FRG_ASSERT(*s);
			}else if(*s == '#') {
				opts.alt_conversion = true;
				++s;
				FRG_ASSERT(*s);
			}else if(*s == '0') {
				opts.fill_zeros = true;
				++s;
				FRG_ASSERT(*s);
			}else if(*s == '\'') {
				opts.group_thousands = true;
				++s;
				FRG_ASSERT(*s);
			}else{
				break;
			}
		}

		if(*s == '*') {
			++s;
			FRG_ASSERT(*s);
			opts.minimum_width = pop_arg<int>(vsp, &opts);
		}else{
			int w = 0;
			while(*s >= '0' && *s <= '9') {
				w = w * 10 + (*s - '0');
				++s;
				FRG_ASSERT(*s);
			}
			opts.minimum_width = w;
		}

		if(*s == '.') {
			++s;
			FRG_ASSERT(*s);

			if(*s == '*') {
				++s;
				FRG_ASSERT(*s);
				opts.precision = pop_arg<int>(vsp, &opts);
			}else{
				int value = 0;
				// If no integer follows the '.', then precision is taken to be zero
				while(*s >= '0' && *s <= '9') {
					value = value * 10 + (*s - '0');
					++s;
					FRG_ASSERT(*s);
				}
				opts.precision = value;
			}
		}

		auto szmod = printf_size_mod::default_size;
		if(*s == 'l') {
			++s;
			FRG_ASSERT(*s);
			if(*s == 'l') {
				szmod = printf_size_mod::longlong_size;
				++s;
				FRG_ASSERT(*s);
			}else{
				szmod = printf_size_mod::long_size;
			}
		}else if(*s == 'z') {
			szmod = printf_size_mod::native_size;
			++s;
			FRG_ASSERT(*s);
		} else if(*s == 'L') {
			szmod = printf_size_mod::longdouble_size;
			++s;
			FRG_ASSERT(*s);
		}

		auto res = agent(*s, opts, szmod);
		if(!res)
			return res;

		++s;
	}

	return {};
}

template<typename F>
void do_printf_chars(F &formatter, char t, format_options opts,
		printf_size_mod szmod, va_struct *vsp) {
	switch(t) {
	case 'p':
		FRG_ASSERT(!opts.fill_zeros);
		FRG_ASSERT(!opts.left_justify);
		FRG_ASSERT(!opts.alt_conversion);
		FRG_ASSERT(opts.minimum_width == 0);
		formatter.append("0x");
		_fmt_basics::print_int(formatter, (uintptr_t)pop_arg<void*>(vsp, &opts), 16);
		break;
	case 'c':
		FRG_ASSERT(!opts.fill_zeros);
		FRG_ASSERT(!opts.alt_conversion);
		FRG_ASSERT(szmod == printf_size_mod::default_size);
		FRG_ASSERT(!opts.precision);
		if (opts.left_justify) {
			formatter.append(pop_arg<char>(vsp, &opts));
			for (int i = 0; i < opts.minimum_width - 1; i++)
				formatter.append(' ');
		} else {
			for (int i = 0; i < opts.minimum_width - 1; i++)
				formatter.append(' ');
			formatter.append(pop_arg<char>(vsp, &opts));
		}
		break;
	case 's': {
		FRG_ASSERT(!opts.fill_zeros);
		FRG_ASSERT(!opts.alt_conversion);

		if(szmod == printf_size_mod::default_size) {
			auto s = (const char *)pop_arg<void*>(vsp, &opts);
			if(!s)
				s = "(null)";

			int length = string_view{s}.size();
			if(opts.precision && *opts.precision < length)
				length = *opts.precision;

			if(opts.left_justify) {
				for(int i = 0; i < length && s[i]; i++)
					formatter.append(s[i]);
				for(int i = length; i < opts.minimum_width; i++)
					formatter.append(' ');
			}else{
				for(int i = length; i < opts.minimum_width; i++)
					formatter.append(' ');
				for(int i = 0; i < length && s[i]; i++)
					formatter.append(s[i]);
			}
		}else{
			FRG_ASSERT(szmod == printf_size_mod::long_size);
			auto s = (const wchar_t *)pop_arg<void*>(vsp, &opts);
			if(!s)
				s = L"(null)";

			int length = basic_string_view<wchar_t>{s}.size();
			if(opts.precision && *opts.precision < length)
				length = *opts.precision;

			if(opts.left_justify) {
				for(int i = 0; i < length && s[i]; i++)
					formatter.append(s[i]);
				for(int i = length; i < opts.minimum_width; i++)
					formatter.append(' ');
			}else{
				for(int i = length; i < opts.minimum_width; i++)
					formatter.append(' ');
				for(int i = 0; i < length && s[i]; i++)
					formatter.append(s[i]);
			}
		}
	} break;
	default:
		FRG_ASSERT(!"Unexpected printf terminal");
	}
}

template<typename F>
void do_printf_ints(F &formatter, char t, format_options opts,
		printf_size_mod szmod, va_struct *vsp, locale_options locale_opts = {}) {
	switch(t) {
	case 'd':
	case 'i': {
		FRG_ASSERT(!opts.alt_conversion);
		long number;
		if(szmod == printf_size_mod::long_size) {
			number = pop_arg<long>(vsp, &opts);
		}else if(szmod == printf_size_mod::longlong_size) {
			number = pop_arg<long long>(vsp, &opts);
		}else if(szmod == printf_size_mod::native_size) {
			number = pop_arg<intptr_t>(vsp, &opts);
		}else{
			FRG_ASSERT(szmod == printf_size_mod::default_size);
			number = pop_arg<int>(vsp, &opts);
		}
		if(opts.precision && *opts.precision == 0 && !number) {
			// print nothing in this case
		}else{
			_fmt_basics::print_int(formatter, number, 10, opts.minimum_width,
					opts.precision ? *opts.precision : 1, opts.fill_zeros ? '0' : ' ',
					opts.left_justify, opts.group_thousands, opts.always_sign,
					opts.plus_becomes_space, false, locale_opts);
		}
	} break;
	case 'o': {
		auto print = [&] (auto number) {
			if (number && opts.alt_conversion)
				formatter.append('0');

			if(opts.precision && *opts.precision == 0 && !number) {
				// print nothing in this case
			}else{
				_fmt_basics::print_int(formatter, number, 8, opts.minimum_width,
						opts.precision ? *opts.precision : 1, opts.fill_zeros ? '0' : ' ',
						opts.left_justify, false, opts.always_sign, opts.plus_becomes_space,
						false, locale_opts);
			}
		};

		if(szmod == printf_size_mod::long_size) {
			print(pop_arg<unsigned long>(vsp, &opts));
		}else if(szmod == printf_size_mod::longlong_size) {
			print(pop_arg<unsigned long long>(vsp, &opts));
		}else if(szmod == printf_size_mod::native_size) {
			print(pop_arg<size_t>(vsp, &opts));
		}else{
			FRG_ASSERT(szmod == printf_size_mod::default_size);
			print(pop_arg<unsigned int>(vsp, &opts));
		}
	} break;
	case 'x': {
		auto print = [&] (auto number) {
			if (number && opts.alt_conversion)
				formatter.append("0x");

			if(opts.precision && *opts.precision == 0 && !number) {
				// print nothing in this case
			}else{
				_fmt_basics::print_int(formatter, number, 16, opts.minimum_width,
						opts.precision ? *opts.precision : 1, opts.fill_zeros ? '0' : ' ',
						opts.left_justify, false, opts.always_sign, opts.plus_becomes_space,
						false, locale_opts);
			}
		};

		if(szmod == printf_size_mod::long_size) {
			print(pop_arg<unsigned long>(vsp, &opts));
		}else if(szmod == printf_size_mod::longlong_size) {
			print(pop_arg<unsigned long long>(vsp, &opts));
		}else if(szmod == printf_size_mod::native_size) {
			print(pop_arg<size_t>(vsp, &opts));
		}else{
			FRG_ASSERT(szmod == printf_size_mod::default_size);
			print(pop_arg<unsigned int>(vsp, &opts));
		}
	} break;
	case 'X': {
		auto print = [&] (auto number) {
			if (number && opts.alt_conversion)
				formatter.append("0X");

			if(opts.precision && *opts.precision == 0 && !number) {
				// print nothing in this case
			}else{
				_fmt_basics::print_int(formatter, number, 16, opts.minimum_width,
						opts.precision ? *opts.precision : 1, opts.fill_zeros ? '0' : ' ',
						opts.left_justify, false, opts.always_sign, opts.plus_becomes_space,
						true, locale_opts);
			}
		};

		if(szmod == printf_size_mod::long_size) {
			print(pop_arg<unsigned long>(vsp, &opts));
		}else if(szmod == printf_size_mod::longlong_size) {
			print(pop_arg<unsigned long long>(vsp, &opts));
		}else if(szmod == printf_size_mod::native_size) {
			print(pop_arg<size_t>(vsp, &opts));
		}else{
			FRG_ASSERT(szmod == printf_size_mod::default_size);
			print(pop_arg<unsigned int>(vsp, &opts));
		}
	} break;
	case 'u': {
		FRG_ASSERT(!opts.alt_conversion);
		if(szmod == printf_size_mod::longlong_size) {
			_fmt_basics::print_int(formatter, pop_arg<unsigned long long>(vsp, &opts),
					10, opts.minimum_width,
					1, opts.fill_zeros ? '0' : ' ',
					opts.left_justify, opts.group_thousands, opts.always_sign,
					opts.plus_becomes_space, false, locale_opts);
		}else if(szmod == printf_size_mod::long_size) {
			_fmt_basics::print_int(formatter, pop_arg<unsigned long>(vsp, &opts),
					10, opts.minimum_width,
					1, opts.fill_zeros ? '0' : ' ',
					opts.left_justify, opts.group_thousands, opts.always_sign,
					opts.plus_becomes_space, false, locale_opts);
		}else if(szmod == printf_size_mod::native_size) {
			_fmt_basics::print_int(formatter, pop_arg<size_t>(vsp, &opts),
					10, opts.minimum_width,
					1, opts.fill_zeros ? '0' : ' ',
					opts.left_justify, opts.group_thousands, opts.always_sign,
					opts.plus_becomes_space, false, locale_opts);
		}else{
			FRG_ASSERT(szmod == printf_size_mod::default_size);
			_fmt_basics::print_int(formatter, pop_arg<unsigned int>(vsp, &opts),
					10, opts.minimum_width,
					1, opts.fill_zeros ? '0' : ' ',
					opts.left_justify, opts.group_thousands, opts.always_sign,
					opts.plus_becomes_space, false, locale_opts);
		}
	} break;
	default:
		FRG_ASSERT(!"Unexpected printf terminal");
	}
}

template<typename F>
void do_printf_floats(F &formatter, char t, format_options opts,
		printf_size_mod szmod, va_struct *vsp, locale_options locale_opts = {}) {
	int precision_or_default = opts.precision.has_value() ? *opts.precision : 6;
	bool use_capitals = true;
	switch(t) {
	case 'f':
		use_capitals = false;
		[[fallthrough]];
	case 'F':
		if (szmod == printf_size_mod::longdouble_size) {
			_fmt_basics::print_float(formatter, pop_arg<long double>(vsp, &opts),
					opts.minimum_width, precision_or_default,
					opts.fill_zeros ? '0' : ' ', opts.left_justify, use_capitals,
					opts.group_thousands, locale_opts);
		} else {
			FRG_ASSERT(szmod == printf_size_mod::default_size);
			_fmt_basics::print_float(formatter, pop_arg<double>(vsp, &opts),
					opts.minimum_width, precision_or_default,
					opts.fill_zeros ? '0' : ' ', opts.left_justify, use_capitals,
					opts.group_thousands, locale_opts);
		}
		break;
	case 'g':
	case 'G':
	case 'e':
	case 'E':
		formatter.append("%f");
		break;
	default:
		FRG_ASSERT(!"Unexpected printf terminal");
	}
}

} // namespace frg
