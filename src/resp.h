#ifndef resp_h_
#define resp_h_

#include <string>
#include <vector>
#include <memory>
#include <algorithm>

enum class RespType {
	NUMBER,
	SIMPLE_STRING,
	BULK_STRING,
	ARRAY,
	SIMPLE_ERROR,
};

struct RespElement {
	RespElement(RespType type) : type(type) {}
	virtual void parse(const char* buff, int len, int& pos) = 0;
	virtual ~RespElement() = default;
	virtual int encode(char* buff, int len) const = 0;
	RespType type;
};

struct Number : public RespElement {
	Number() : RespElement(RespType::NUMBER) {}
	Number(int num) : num(num), RespElement(RespType::NUMBER) {}
	void parse(const char* buff, int len, int& pos) override;
	// `len` is the allocated length for the buffer.
	int encode(char* buff, int len) const override;
	
	int num;
};

struct BulkString : public RespElement {
	BulkString() : RespElement(RespType::BULK_STRING) {}
	BulkString(const std::string& str) : str(str), RespElement(RespType::BULK_STRING) {}
	void parse(const char* buff, int len, int& pos) override;
	int encode(char* buff, int len) const override;
	
	std::string str;
};

struct SimpleString : public RespElement {
	SimpleString() : RespElement(RespType::SIMPLE_STRING) {}
	SimpleString(const std::string& str) : str(str), RespElement(RespType::SIMPLE_STRING) {}
	void parse(const char* buff, int len, int& pos) override;
	int encode(char* buff, int len) const override;
	
	std::string str;
};

struct SimpleError : public RespElement {
	SimpleError() : RespElement(RespType::SIMPLE_ERROR) {}
	SimpleError(const std::string& err_msg) : err_msg(err_msg), RespElement(RespType::SIMPLE_ERROR) {}
	void parse(const char* buff, int len, int& pos) override;
	int encode(char* buff, int len) const override;
	
	std::string err_msg;
};

struct RespArray : public RespElement {
	RespArray() : RespElement(RespType::ARRAY) {}
	RespArray(const std::vector<std::shared_ptr<RespElement>>& elts) : elts(elts),
	RespElement(RespType::ARRAY) {}
	void parse(const char* buff, int len, int& pos) override;
	int encode(char* buff, int len) const override;

	std::vector<std::shared_ptr<RespElement>> elts;
};

std::unique_ptr<RespElement> Parse(const char* buff, int len, int& pos);


#endif
