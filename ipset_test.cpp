#include <iostream>  
#include <thread>     // std::thread  
#include <mutex>     // std::mutex  
#include <assert.h>  
extern "C" {  

    // Debian libipset libary is a C library. All the headers  
    // have to be enclosed in extern "C" brackets to have  
    // proper linkage  
#include <libipset/session.h>      /* ipset_session_* */  
#include <libipset/data.h>       /* enum ipset_data */  
#include <libipset/parse.h>       /* ipset_parse_* */  
#include <libipset/types.h>       /* struct ipset_type */  
#include <libipset/ui.h>        /* core options, commands */  
#include <libipset/utils.h>       /* STREQ */  
}  
// debian-ipset is not thread-safe. We're using  
// a mutex to serialize the ipset calls.  
std::mutex s_ipsetMutex;  
// We're only adding a command/an element to the set,  
// so the restore line no. remains 0  
#define IPSET_SESSION_LINE_NO 0  
    int  
ipsetAdd(std::string table, std::string set_type, uint32_t interval)  
{  
    int32_t ret = 0;  
    const struct ipset_type *type = NULL;  
    enum ipset_cmd cmd = IPSET_CMD_ADD;  
    struct ipset_session *session;  
    // debian-ipset calls are not thread-safe. We're adding  
    // a mutex to serialize the calls.  
    s_ipsetMutex.lock();  
    ipset_load_types();  
    // Initialize an ipset session by allocating a session structure  
    // and filling out with the initialization data.  
    session = ipset_session_init(printf);  
    if (session == NULL)  
    {  
        printf("ipsetAdd: Cannot initialize ipset session for "  
                "set_type %s. aborting.\n", set_type.c_str());  
        ret = 1;  
    }  
    else  
    {  
        // Set session lineno to report parser errors correctly   
        ipset_session_lineno(session, IPSET_SESSION_LINE_NO);  
        // Parse string as a setname.  
        // The value is stored in the data blob of the session.  
        ret = ipset_parse_setname(session, IPSET_SETNAME, table.c_str());  
        if (ret == 0)  
        {  
            type = ipset_type_get(session, cmd);  
            if (!type)  
            {  
                printf("ipsetAdd: Failed to get the set type from Kernel "  
                        "set_type=%s table=%s sess=%p cmd=%d\n", set_type.c_str(), table.c_str(),  
                        session, cmd);  
                ret = 1;  
            }  
            else  
            {  
                // Parse string as a (multipart) element according to the settype.  
                // The value is stored in the data blob of the session.  
                ret = ipset_parse_elem(session, (ipset_opt)type->last_elem_optional,  
                        set_type.c_str());  
                if (ret == 0)  
                {  
                    // Put a given kind of data into the data blob and mark the  
                    // option kind as already set in the blob.  
                    ret = ipset_data_set(ipset_session_data(session), IPSET_OPT_TIMEOUT, &interval);  
                    if (!ret)  
                    {  
                        // Execute a command  
                        ret = ipset_cmd(session, cmd, IPSET_SESSION_LINE_NO);  
                        if (ret)  
                        {  
                            printf("ipsetAdd: WARNING: Failed to execute "  
                                    "IPSET_CMD_ADD command. ipset exists? ret=%d set_type=%s table=%s\n",  
                                    ret, set_type.c_str(), table.c_str());  
                            // Not an error condition  
                            ret = 0;  
                        }  
                    }  
                    else  
                    {  
                        printf("ipsetAdd: Failed to set timeout option. "  
                                "ret=%d set_type=%s table=%s\n", ret, set_type.c_str(), table.c_str());  
                    }  
                }  
                else  
                {  
                    printf("ipsetAdd: Failed to parse element. "  
                            "ret=%d set_type=%s table=%s\n", ret, set_type.c_str(), table.c_str());  
                }  
            }  
        }  
        else  
        {  
            printf("ipsetAdd: Failed to parse setname. "  
                    "ret=%d set_type=%s table=%s\n", ret, set_type.c_str(), table.c_str());  
        }  
        // Finish the ipset session  
        if (ipset_session_fini(session) != 0)  
        {  
            printf("ipsetAdd: Failed to destroy ipset "  
                    "session. ret=%d set_type=%s table=%s\n", ret, set_type.c_str(),  
                    table.c_str());  
        }  
    } // else of if (session == NULL)  
    s_ipsetMutex.unlock();  
    return ret;  
}  
int main()  
{  
    ipsetAdd("my_ipset_table", "127.0.0.1,62142,127.0.0.1", 600);  
    return 0;  
}  

