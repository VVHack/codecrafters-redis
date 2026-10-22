#include "command.h"
#include "echo.h"
#include "ping.h"
#include "set.h"
#include "lpop.h"
#include "blpop.h"
#include "lpush.h"
#include "xadd.h"
#include "xrange.h"
#include "llen.h"
#include "type.h"
#include "get.h"
#include "rpush.h"
#include "lrange.h"
#include "resp.h"

// Parses a single client request off the wire and classifies it as a Command.
//
// The raw bytes are first decoded into a RESP tree by Parse(). Two shapes are
// accepted:
//   - a bare bulk string, which is only ever the inline "PING" case, and
//   - an array whose first element is the command name and whose remaining
//     elements are the arguments (the normal form real clients send).
//
// Each branch validates both the arity and the element types it needs before
// claiming the command, so downstream handlers can index into the array and
// dynamic_cast to BulkString without re-checking. Arity rules mirror Redis:
// exact counts for fixed commands (GET, LLEN, TYPE, LRANGE, BLPOP, XRANGE),
// minimums for variadic ones (SET with its options, RPUSH/LPUSH with multiple
// values, LPOP with an optional count), and XADD additionally requires an odd
// total size so the trailing field/value pairs are balanced.
//
// Anything that fails validation — an unknown name, wrong arity, or a
// non-bulk-string where one is required — falls through to Command::UNKNOWN
// rather than throwing. Note this is deliberately silent: a malformed SET is
// reported the same way as a command we simply do not implement.
//
// Returns the classification paired with ownership of the parsed RESP tree, so
// the caller can read the arguments out of it. The tree is returned even for
// UNKNOWN. Command name matching is case-sensitive (uppercase only), unlike
// real Redis.
//
// req/len are the request bytes; the parse starts at offset 0 and any bytes
// after the first complete RESP element are ignored.
std::unique_ptr<Command> ParseCommand(const char* req, int len) {
	int pos = 0;
	std::unique_ptr<RespElement> resp = Parse(req, len, pos);
	if (resp->type == RespType::BULK_STRING &&
			dynamic_cast<BulkString*>(resp.get())->str == "PING") {
		return std::make_unique<Ping>(std::shared_ptr<RespElement>(resp.release()));
	}
	if (resp->type == RespType::ARRAY) {
		auto arr = dynamic_cast<RespArray*>(resp.get());
		if (arr->elts.size() == 2 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "ECHO") {
			if (arr->elts[1]->type == RespType::BULK_STRING) {
				return std::make_unique<Echo>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() == 1 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "PING") {
			return std::make_unique<Ping>(std::shared_ptr<RespElement>(resp.release()));
		} else if (arr->elts.size() >= 3 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "SET") {
			if (arr->elts[1]->type == RespType::BULK_STRING && arr->elts[2]->type == RespType::BULK_STRING) {
				return std::make_unique<Set>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() == 2 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "GET") {
			if (arr->elts[1]->type == RespType::BULK_STRING) {
				return std::make_unique<Get>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() >= 3 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "RPUSH") {
			if (arr->elts[1]->type == RespType::BULK_STRING) {
				return std::make_unique<Rpush>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() >= 3 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "LPUSH") {
			if (arr->elts[1]->type == RespType::BULK_STRING) {
				return std::make_unique<Lpush>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() >= 2 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "LPOP") {
			if (arr->elts[1]->type == RespType::BULK_STRING) {
				return std::make_unique<Lpop>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() == 3 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "BLPOP") {
			if (arr->elts[1]->type == RespType::BULK_STRING) {
				return std::make_unique<Blpop>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() == 4 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "LRANGE") {
			if (arr->elts[1]->type == RespType::BULK_STRING &&
					arr->elts[2]->type == RespType::BULK_STRING && arr->elts[3]->type == RespType::BULK_STRING) {
				return std::make_unique<Lrange>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() == 2 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "LLEN") {
			if (arr->elts[1]->type == RespType::BULK_STRING) {
				return std::make_unique<Llen>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() == 2 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "TYPE") {
			if (arr->elts[1]->type == RespType::BULK_STRING) {
				return std::make_unique<Type>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() >= 3 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "XADD") {
			if (arr->elts[1]->type == RespType::BULK_STRING &&
					arr->elts[2]->type == RespType::BULK_STRING && arr->elts.size() % 2 == 1) {
				return std::make_unique<Xadd>(std::shared_ptr<RespElement>(resp.release()));
			}
		} else if (arr->elts.size() == 4 && arr->elts[0]->type == RespType::BULK_STRING &&
				dynamic_cast<BulkString*>(arr->elts[0].get())->str == "XRANGE") {
			return std::make_unique<Xrange>(std::shared_ptr<RespElement>(resp.release()));
		}
	}
	return nullptr;
	//return {Command::UNKNOWN, std::move(resp)};
}

