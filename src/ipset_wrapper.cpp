// ipset_wrapper.cpp
//
// See include/ipset_wrapper.h for the public API and usage notes.
//
// Every classification of "what actually happened" below (AlreadyPresent
// vs NotPresent vs a genuine Error) was verified empirically against a
// live kernel ip_set (libipset 7.19 / Ubuntu 24.04), not guessed from the
// header comments -- the exact ipset_session_report_type()/report_msg()
// values returned for each case are what drive runCmd()'s branches.

#include "ipset_wrapper.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ipsettarpit {

namespace {

// libipset's session wants a printf-style callback that also receives the
// session and a user pointer. We route it straight to vprintf; swap this
// out for your own logger if you don't want ipset's own list/save output
// going to stdout.
int sessionPrintf(struct ipset_session* /*session*/, void* /*user*/, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = std::vprintf(fmt, ap);
    va_end(ap);
    return n;
}

}  // namespace

IpsetSet::IpsetSet() {
    // Cheap and idempotent; safe to call once per process. Must happen
    // before any ipset_type_get()/ipset_parse_typename() call, or type
    // lookups silently fail.
    ipset_load_types();
}

IpsetSet::~IpsetSet() = default;

IpsetResult IpsetSet::ensureSet(const std::string& setName, const std::string& setType,
                                uint32_t defaultTimeoutSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    struct ipset_session* session = ipset_session_init(sessionPrintf, nullptr);
    if (!session) {
        lastError_ = "ensureSet: ipset_session_init failed";
        return IpsetResult::Error;
    }
    ipset_session_lineno(session, 0);

    // Mirrors `ipset -exist create ...`: creating a set that already
    // exists with the SAME type/params is treated as success instead of
    // an error. A genuine type mismatch on an existing name still fails
    // (verified: -exist does not paper over that case).
    ipset_envopt_set(session, IPSET_ENV_EXIST);

    IpsetResult result = IpsetResult::Ok;
    int ret = ipset_parse_setname(session, IPSET_SETNAME, setName.c_str());
    if (ret != 0) {
        lastError_ = "ensureSet: invalid set name '" + setName + "'";
        result = IpsetResult::Error;
    } else {
        ret = ipset_parse_typename(session, IPSET_OPT_TYPENAME, setType.c_str());
        if (ret != 0) {
            lastError_ = "ensureSet: invalid/unsupported set type '" + setType + "'";
            result = IpsetResult::Error;
        } else if (!ipset_type_get(session, IPSET_CMD_CREATE)) {
            lastError_ = std::string("ensureSet: ") + (ipset_session_report_msg(session)
                                                           ? ipset_session_report_msg(session)
                                                           : "unknown type resolution failure");
            result = IpsetResult::Error;
        } else {
            if (defaultTimeoutSeconds > 0) {
                ipset_data_set(ipset_session_data(session), IPSET_OPT_TIMEOUT,
                               &defaultTimeoutSeconds);
            }
            ret = ipset_cmd(session, IPSET_CMD_CREATE, 0);
            if (ret != 0) {
                const char* msg = ipset_session_report_msg(session);
                lastError_ = std::string("ensureSet: ") + (msg ? msg : "create failed");
                result = IpsetResult::Error;
            } else {
                lastError_.clear();
            }
        }
    }

    ipset_session_fini(session);
    return result;
}

IpsetResult IpsetSet::add(const std::string& setName, const std::string& element,
                          uint32_t timeoutSeconds) {
    return runCmd(IPSET_CMD_ADD, setName, element, timeoutSeconds, "");
}

IpsetResult IpsetSet::del(const std::string& setName, const std::string& element) {
    return runCmd(IPSET_CMD_DEL, setName, element, 0, "");
}

IpsetResult IpsetSet::test(const std::string& setName, const std::string& element) {
    return runCmd(IPSET_CMD_TEST, setName, element, 0, "");
}

IpsetResult IpsetSet::runCmd(enum ipset_cmd cmd, const std::string& setName,
                             const std::string& element, uint32_t timeoutSeconds,
                             const std::string& /*setType*/) {
    std::lock_guard<std::mutex> lock(mutex_);

    struct ipset_session* session = ipset_session_init(sessionPrintf, nullptr);
    if (!session) {
        lastError_ = "ipset_session_init failed";
        return IpsetResult::Error;
    }
    ipset_session_lineno(session, 0);

    int ret = ipset_parse_setname(session, IPSET_SETNAME, setName.c_str());
    if (ret != 0) {
        lastError_ = "set '" + setName + "' does not exist or invalid name";
        ipset_session_fini(session);
        return IpsetResult::Error;
    }

    const struct ipset_type* type = ipset_type_get(session, cmd);
    if (!type) {
        const char* msg = ipset_session_report_msg(session);
        lastError_ = msg ? msg : ("set '" + setName + "' not found");
        ipset_session_fini(session);
        return IpsetResult::Error;
    }

    ret = ipset_parse_elem(session, type->last_elem_optional, element.c_str());
    if (ret != 0) {
        const char* msg = ipset_session_report_msg(session);
        lastError_ = msg ? msg : ("could not parse element '" + element + "'");
        ipset_session_fini(session);
        return IpsetResult::Error;
    }

    if (cmd == IPSET_CMD_ADD && timeoutSeconds > 0) {
        ipset_data_set(ipset_session_data(session), IPSET_OPT_TIMEOUT, &timeoutSeconds);
    }

    ret = ipset_cmd(session, cmd, 0);

    IpsetResult result;
    if (ret == 0) {
        lastError_.clear();
        result = IpsetResult::Ok;
    } else {
        const enum ipset_err_type reportType = ipset_session_report_type(session);
        const char* msg = ipset_session_report_msg(session);
        const std::string msgStr = msg ? msg : "";

        // These branches were verified against the live kernel, not
        // inferred from docs: ADD-on-duplicate and DEL-on-missing both
        // report IPSET_ERROR with a specific message; TEST-on-missing
        // reports IPSET_NOTICE. Everything else is a genuine failure
        // (bad type, no permission, kernel module not loaded, etc).
        if (cmd == IPSET_CMD_ADD && reportType == IPSET_ERROR &&
            msgStr.find("already added") != std::string::npos) {
            lastError_.clear();
            result = IpsetResult::AlreadyPresent;
        } else if (cmd == IPSET_CMD_DEL && reportType == IPSET_ERROR &&
                   msgStr.find("not added") != std::string::npos) {
            lastError_.clear();
            result = IpsetResult::NotPresent;
        } else if (cmd == IPSET_CMD_TEST && reportType == IPSET_NOTICE) {
            lastError_.clear();
            result = IpsetResult::NotPresent;
        } else {
            lastError_ = msgStr.empty() ? "ipset_cmd failed" : msgStr;
            result = IpsetResult::Error;
        }
    }

    ipset_session_fini(session);
    return result;
}

}  // namespace ipsettarpit
