#include <cstring>
#include <cerrno>
#include <cassert>
#include <cstdlib>

#include <iconv.h>

#define I_ACKNOWLEDGE_THAT_NATUS_IS_NOT_STABLE
#include <natus/natus.hpp>
using namespace natus;

#define OK(x) ok = (!x.isException()) || ok
#define NCONST(macro) OK(base.setRecursive("exports." # macro, (long) macro))
#define NFUNC(func) OK(base.setRecursive("exports." # func, posix_ ## func))

#define PRIV_BINARY_BUFFER "commonjs::binary"

static Value convert(Value ctx, const char *from, const char *to, size_t srclen, const unsigned char* srcbuf, size_t* dstlen, unsigned char** dstbuf) {
	iconv_t state = iconv_open(from, to);
	if (state < 0) return throwException(ctx, errno);

	*dstbuf = NULL;
	*dstlen = 0;
	size_t chars = -1, bytesin = srclen, bytesout = 0;
	char *tmpsrc, *tmpdst;
	errno = E2BIG;
	do {
		if (errno != E2BIG) break;

		// Reallocate the buffer
		*dstlen += bytesin;
		unsigned char *tmp = (unsigned char*) realloc(*dstbuf, *dstlen);
		if (!tmp) break;
		*dstbuf = tmp;

		// Reset the state
		tmpsrc = (char*)  srcbuf;
		tmpdst = (char*) *dstbuf;
		bytesin  =  srclen;
		bytesout = *dstlen;
		iconv(state, NULL, NULL, NULL, NULL);
	} while ((chars = iconv(state, &tmpsrc, &bytesin, &tmpdst, &bytesout)) < 0);
	iconv_close(state);

	// If we have an unrecoverable error, throw it
	if (chars < 0) {
		free(*dstbuf);
		return throwException(ctx, errno);
	}

	*dstlen -= bytesout; // Decrease the length by the amount of buffer not filled
	Value undef = ctx.newUndefined();
	if (undef.isException()) free(*dstbuf);
	return undef;
}

static void free_buffer(unsigned char *buf) {
	delete[] buf;
}

class BinaryStringClass : public Class {
public:
	virtual Class::Flags getFlags () {
		return Class::FlagObject;
	}

	virtual Value del(Value& obj, Value& name) {
		if (!name.isNumber()) return NULL;
		return throwException(obj, "IndexError", "ByteStrings are immutable!");
	}

	virtual Value set(Value& obj, Value& name, Value& value) {
		if (!name.isNumber()) return NULL;
		return throwException(obj, "IndexError", "ByteStrings are immutable!");
	}

	virtual Value get(Value& obj, Value& name) {
		if (!name.isNumber()) return NULL;

		ssize_t idx = name.to<ssize_t>();
		ssize_t len = obj.get("length").to<ssize_t>();
		if (idx < 0) idx += len; // Convert -1 to (len-1)
		if (idx < 0)    return throwException(obj, "IndexError", "Negative index is before the start of the array!");
		if (idx >= len) return obj.newUndefined();

		const unsigned char* tmp = obj.getPrivate<const unsigned char*>(PRIV_BINARY_BUFFER);
		if (!tmp) return obj.newUndefined();

		return obj.newNumber(tmp[name.to<size_t>()]);
	}

	virtual Value enumerate(Value& obj) {
		size_t len = obj.get("length").to<size_t>();

		Value items = obj.newArray();
		for (size_t i=0 ; i < len ; i++)
			arrayBuilder(items, (double) i);

		return items;
	}
};

class BinaryArrayClass : public BinaryStringClass {
public:
	virtual Value del(Value& obj, Value& name) {
		if (!name.isNumber()) return NULL;

		ssize_t idx = name.to<ssize_t>();
		ssize_t len = obj.get("length").to<ssize_t>();
		if (idx < 0) idx += len; // Convert -1 to (len-1)
		if (idx < 0) return throwException(obj, "IndexError", "Negative index is before the start of the array!");

		unsigned char* buf = obj.getPrivate<unsigned char*>(PRIV_BINARY_BUFFER);
		if (buf) memmove(buf+idx, buf+idx+1, len-idx-1);
		return obj.newUndefined();
	}

	virtual Value set(Value& obj, Value& name, Value& value) {
		if (!name.isNumber()) return NULL;

		ssize_t idx = name.to<ssize_t>();
		ssize_t val = value.to<size_t>();
		ssize_t len = obj.get("length").to<ssize_t>();
		if (idx < 0) idx += len; // Convert -1 to (len-1)
		if (idx < 0)              return throwException(obj, "IndexError", "Negative index is before the start of the array!");
		if (!value.isNumber())    return throwException(obj, "TypeError",  "Value must be a number!");
		if (val < 0 || val > 255) return throwException(obj, "RangeError", "Byte values must be between 0 and 255 inclusive!");

		// If the buffer doesn't exist or needs to be resized
		unsigned char* buf = obj.getPrivate<unsigned char*>(PRIV_BINARY_BUFFER);
		if (!buf || idx >= len) {
			unsigned char* tmp = new unsigned char[idx+1];
			memset(tmp, 0, idx+1);
			if (buf) memcpy(tmp, buf, len);
			if (!obj.setPrivate(PRIV_BINARY_BUFFER, tmp, (FreeFunction) free_buffer))
				return obj.newUndefined();
			buf = tmp;
			len = idx+1;
			obj.set("length", len, Value::PropAttrProtected);
		}

		buf[idx] = val;
		return obj.newUndefined();
	}
};

static Value binary_Binary(Value& fnc, Value& ths, Value& arg) {
	return throwException(fnc, "ValueError", "Binary is abstract!");
}

static Value binary_genericConstructor(Value& obj, Value& arg, bool supplen) {
	if (supplen)
		NATUS_CHECK_ARGUMENTS(arg, "|(oasn)s")
	else
		NATUS_CHECK_ARGUMENTS(arg, "|(oas)s")
	unsigned char *buf = NULL;
	size_t         len = 0;

	// Handles: Byte*(length)
	if (supplen && arg[0].isNumber()) {
		len = arg[0].to<size_t>();
		buf = new unsigned char[len];
	}

	// Handles: Byte*(byteString) and Byte*(byteArray)
	else if (arg[0].isObject()) {
		const unsigned char* tmp = arg[0].getPrivate<const unsigned char*>(PRIV_BINARY_BUFFER);
		if (tmp) {
			len = arg[0].get("length").to<size_t>();
			buf = new unsigned char[len];
			memcpy(buf, tmp, len);
		}
	}

	// Handles: Byte*(arrayOfNumbers)
	else if (arg[0].isArray()) {
		len = arg[0].get("length").to<size_t>();
		buf = new unsigned char[len];
		for (size_t i=0 ; i < len ; i++) {
			ssize_t d = arg[0][i].to<ssize_t>();
			if (d < 0 || d > 255) {
				delete[] buf;
				delete buf;
				return throwException(arg, "RangeError", "Byte values must be between 0 and 255 inclusive!");
			}
			buf[i] = d;
		}
	}

	// Handles: Byte*(string, charset)
	else if (arg.get("length").to<size_t>() > 1 && arg[0].isString() && arg[1].isString()) {
		UTF16 data = arg[0].to<UTF16>();
		Value rslt = convert(arg, "UCS-2-INTERNAL", arg[1].to<UTF8>().c_str(), data.length(), (unsigned char*) data.c_str(), &len, &buf);
		if (rslt.isException()) return rslt;
	}

	obj.setPrivate(PRIV_BINARY_BUFFER, buf, (FreeFunction) free_buffer);
	obj.set("length", (double) len, Value::PropAttrProtected);
	return obj;
}

static Value binary_ByteString(Value& fnc, Value& ths, Value& arg) {
	Value obj = fnc.newObject(new BinaryStringClass);
	return binary_genericConstructor(obj, arg, false);
}

static Value _join(Value& arg) {
	NATUS_CHECK_ARGUMENTS(arg, "an");

	size_t len = arg[0].get("length").to<size_t>();

	Value array = arg.newArray();
	for (size_t i=0 ; i < len ; i++) {
		arrayBuilder(array, arg[0][i]);
		if (i+1 < len) arrayBuilder(array, arg[1]);
	}

	Value *data[2] = { &array, NULL };
	return arg.newArray(data);
}

static Value binary_ByteArray(Value& fnc, Value& ths, Value& arg) {
	Value obj = fnc.newObject(new BinaryArrayClass);
	return binary_genericConstructor(obj, arg, true);
}

static Value binary_ByteString__join(Value& fnc, Value& ths, Value& arg) {
	Value args = _join(arg);
	if (args.isException()) return args;
	return binary_ByteString(fnc, ths, args);
}

static Value binary_ByteArray__join(Value& fnc, Value& ths, Value& arg) {
	Value args = _join(arg);
	if (args.isException()) return args;
	return binary_ByteArray(fnc, ths, args);
}

static Value binary_ByteString_toByteArray(Value& fnc, Value& ths, Value& arg) {
	NATUS_CHECK_ARGUMENTS(arg, "|ss");
	size_t         arglen = arg.get("length").to<size_t>();

	size_t         srclen = ths.get("length").to<size_t>();
	unsigned char* srcbuf = ths.getPrivate<unsigned char*>(PRIV_BINARY_BUFFER);

	Value tmp = fnc.newArray();
	Value obj = binary_ByteArray(fnc, ths, tmp);
	if (obj.isException() || !srcbuf || srclen == 0) return obj;

	if (arglen == 0) {
		unsigned char* tmp = new unsigned char[srclen];
		memcpy(tmp, srcbuf, srclen);
		obj.setPrivate(PRIV_BINARY_BUFFER, tmp, (FreeFunction) free_buffer);
		obj.set("length", (double) srclen, Value::PropAttrProtected);
		return ths;
	}

	if (arglen == 1)
		return throwException(fnc, "ValueError", "ByteString.toByteString(src_encoding, dst_encoding) requires two arguments!");

	size_t len;
	unsigned char* buf;
	Value rslt = convert(ths, arg[0].to<UTF8>().c_str(), arg[1].to<UTF8>().c_str(), srclen, srcbuf, &len, &buf);
	if (rslt.isException()) return rslt;

	obj.setPrivate(PRIV_BINARY_BUFFER, buf, (FreeFunction) free_buffer);
	obj.set("length", (double) len);
	return obj;
}

static Value binary_ByteString_toByteString(Value& fnc, Value& ths, Value& arg) {
	NATUS_CHECK_ARGUMENTS(arg, "|ss");
	size_t arglen = arg.get("length").to<size_t>();
	const unsigned char* srcbuf = ths.getPrivate<const unsigned char*>(PRIV_BINARY_BUFFER);

	if (arglen == 0)
		return ths;

	if (arglen == 1)
		return throwException(fnc, "ValueError", "ByteString.toByteString(src_encoding, dst_encoding) requires two arguments!");

	Value tmp = fnc.newArray();
	Value obj = binary_ByteString(fnc, ths, tmp);
	if (obj.isException() || !srcbuf) return obj;

	size_t len;
	unsigned char* buf;
	Value rslt = convert(ths, arg[0].to<UTF8>().c_str(), arg[1].to<UTF8>().c_str(), ths.get("length").to<size_t>(), srcbuf, &len, &buf);
	if (rslt.isException()) return rslt;

	obj.setPrivate(PRIV_BINARY_BUFFER, buf, (FreeFunction) free_buffer);
	obj.set("length", (double) len);
	return obj;
}

extern "C" bool NATUS_MODULE_INIT(ntValue* base) {
	Value module(base, false);
	Value exports = module.get("exports");

	exports.set("Binary", binary_Binary);
	exports.setRecursive("ByteString",                          binary_ByteString);
	exports.setRecursive("ByteString.join",                     binary_ByteString__join);
	exports.setRecursive("ByteString.prototype.toByteArray",    binary_ByteString_toByteArray);
	exports.setRecursive("ByteString.prototype.toByteString",   binary_ByteString_toByteString);
	exports.setRecursive("ByteString.prototype.toArray",        binary_ByteString_toArray);
	exports.setRecursive("ByteString.prototype.toString",       binary_ByteString_toString);
	exports.setRecursive("ByteString.prototype.toSource",       binary_ByteString_toSource);
	exports.setRecursive("ByteString.prototype.decodeToString", binary_ByteString_decodeToString);
	exports.setRecursive("ByteString.prototype.indexOf",        binary_ByteString_indexOf);
	exports.setRecursive("ByteString.prototype.lastIndexOf",    binary_ByteString_lastIndexOf);
	exports.setRecursive("ByteString.prototype.codeAt",         binary_ByteString_codeAt);
	exports.setRecursive("ByteString.prototype.byteAt",         binary_ByteString_byteAt);
	exports.setRecursive("ByteString.prototype.valueAt",        binary_ByteString_valueAt);
	exports.setRecursive("ByteString.prototype.get",            binary_ByteString_get);
	exports.setRecursive("ByteString.prototype.copy",           binary_ByteString_copy);
	exports.setRecursive("ByteString.prototype.split",          binary_ByteString_split);
	exports.setRecursive("ByteString.prototype.slice",          binary_ByteString_slice);
	exports.setRecursive("ByteString.prototype.concat",         binary_ByteString_concat);
	exports.setRecursive("ByteString.prototype.substr",         binary_ByteString_substr);
	exports.setRecursive("ByteString.prototype.substring",      binary_ByteString_substring);

	exports.setRecursive("ByteArray",                           binary_ByteArray);
	exports.setRecursive("ByteArray.join",                      binary_ByteArray__join);
	exports.setRecursive("ByteString.prototype.toByteArray",    binary_ByteArray_toByteArray);
	exports.setRecursive("ByteString.prototype.toByteString",   binary_ByteArray_toByteString);
	exports.setRecursive("ByteString.prototype.toArray",        binary_ByteArray_toArray);
	exports.setRecursive("ByteString.prototype.toString",       binary_ByteArray_toString);
	exports.setRecursive("ByteString.prototype.toSource",       binary_ByteArray_toSource);
	exports.setRecursive("ByteString.prototype.decodeToString", binary_ByteArray_decodeToString);
	exports.setRecursive("ByteString.prototype.codeAt",         binary_ByteArray_codeAt);
	exports.setRecursive("ByteString.prototype.byteAt",         binary_ByteArray_byteAt);
	exports.setRecursive("ByteString.prototype.valueAt",        binary_ByteArray_valueAt);
	exports.setRecursive("ByteString.prototype.get",            binary_ByteArray_get);
	exports.setRecursive("ByteString.prototype.copy",           binary_ByteArray_copy);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_fill);
	exports.setRecursive("ByteString.prototype.concat",         binary_ByteArray_concat);
	exports.setRecursive("ByteString.prototype.pop",            binary_ByteArray_pop);
	exports.setRecursive("ByteString.prototype.push",           binary_ByteArray_push);
	exports.setRecursive("ByteString.prototype.extendRight",    binary_ByteArray_extendRight);
	exports.setRecursive("ByteString.prototype.shift",          binary_ByteArray_shift);
	exports.setRecursive("ByteString.prototype.unshift",        binary_ByteArray_unshift);
	exports.setRecursive("ByteString.prototype.extendLeft",     binary_ByteArray_extendLeft);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_reverse);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_slice);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_sort);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_splice);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_indexOf);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_lastIndexOf);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_split);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_filter);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_forEach);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_every);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_some);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_map);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_reduce);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_reduceRight);
	exports.setRecursive("ByteString.prototype.fill",           binary_ByteArray_displace);

	return true;
}
