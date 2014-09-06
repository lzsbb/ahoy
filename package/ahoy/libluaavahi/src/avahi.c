/* 

Lua Avahi Library

License: All Rights Reserved (for now)

Authors:       v0.01, Dean Blackketter, Ahoy

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define AVAHI_LUA_VERSION "0.1"
#define AVAHI_LUA_AUTHORS "Dean Blackketter"

//------------------------------------------------------------------------------
// Prototypes
//------------------------------------------------------------------------------

LUALIB_API int luaopen_avahi(lua_State *L);
LUALIB_API int avahi_version(lua_State *L);
LUALIB_API int avahi_help(lua_State *L);
LUALIB_API int avahi_browse(lua_State *L);
LUALIB_API int avahi_open(lua_State *L);
LUALIB_API int avahi_close(lua_State *L);
LUALIB_API int avahi_poll(lua_State *L);
LUALIB_API int avahi_found(lua_State *L);
LUALIB_API int avahi_status(lua_State *L);
void* get_environ_ud(lua_State *L, const char* name);

//------------------------------------------------------------------------------

/* functions exposed to lua */


static const luaL_reg avahi_functions[] = {
  {"version", avahi_version},
  {"help", avahi_help},
  {"open", avahi_open},
  {"poll", avahi_poll},
  {"browse", avahi_browse},
  {"close", avahi_close}, 
  {"found", avahi_found},
  {"status", avahi_status},
  {NULL, NULL}
};

//------------------------------------------------------------------------------
// Utility
//------------------------------------------------------------------------------
void* get_environ_ud(lua_State *L, const char* name) {
  lua_getfield(L, LUA_ENVIRONINDEX, name);
  void** data = (void**)lua_touserdata (L, -1);
  return *data;
}

//------------------------------------------------------------------------------
// Callbacks
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------

static void resolve_callback(
    AvahiServiceResolver *r,
    AVAHI_GCC_UNUSED AvahiIfIndex interface,
    AVAHI_GCC_UNUSED AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *address,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    void* userdata) {

    lua_State* L = userdata;
    
    assert(r);

    /* Called whenever a service has been resolved successfully or timed out */

    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
//            fprintf(stderr, "(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s\n", name, type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
            break;

        case AVAHI_RESOLVER_FOUND: {
            char a[AVAHI_ADDRESS_STR_MAX], *t;

//            fprintf(stderr, "Service '%s' of type '%s' in domain '%s':\n", name, type, domain);

            avahi_address_snprint(a, sizeof(a), address);
            t = avahi_string_list_to_string(txt);
            
            lua_getfield(L, LUA_ENVIRONINDEX, "found.table");
            
            // this table holds the data
            lua_newtable(L);
            
            
            lua_pushstring(L, name);
            lua_setfield(L, -2, "name");
            
            lua_pushstring(L, type);
            lua_setfield(L, -2, "type");
            
            lua_pushstring(L, domain);
            lua_setfield(L, -2, "domain");
            
            lua_pushstring(L, host_name);
            lua_setfield(L, -2, "host_name");
            
            lua_pushinteger(L, port);
            lua_setfield(L, -2, "port");
           
            lua_pushstring(L, a);
            lua_setfield(L, -2, "address");
            
            lua_pushstring(L, t);
            lua_setfield(L, -2, "txt");
           
            lua_pushinteger(L, avahi_string_list_get_service_cookie(txt));
            lua_setfield(L, -2, "cookie");
          
            lua_pushboolean (L, !!(flags & AVAHI_LOOKUP_RESULT_LOCAL));
            lua_setfield(L, -2, "local");
      
            lua_pushboolean (L, !!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN));
            lua_setfield(L, -2, "own");
      
            lua_pushboolean (L, !!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA));
            lua_setfield(L, -2, "widearea");
      
            lua_pushboolean (L,  !!(flags & AVAHI_LOOKUP_RESULT_MULTICAST));
            lua_setfield(L, -2, "multicast");
      
            lua_pushboolean (L, !!(flags & AVAHI_LOOKUP_RESULT_CACHED));
            lua_setfield(L, -2, "cached");
      
            char* key = malloc(strlen(name)+strlen(type)+strlen(domain)+2);
            sprintf(key, "%s:%s:%s", name, type, domain);
            lua_setfield(L, -2, key);
            free(key);
                        
            // pop off the found.table
            lua_pop(L, 1);

            avahi_free(t);
        }
    }

    avahi_service_resolver_free(r);
}

static void browse_callback(
    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    void* userdata) {

    lua_State* L = userdata;
    
    AvahiClient *c = get_environ_ud(L, "avahi.client");
    
    AvahiSimplePoll *simple_poll = get_environ_ud(L, "avahi.simplepoll");

    assert(b);
    assert(c);
    assert(simple_poll);
    
    /* Called whenever a new services becomes available on the LAN or is removed from the LAN */

    switch (event) {
        case AVAHI_BROWSER_FAILURE:

//            fprintf(stderr, "(Browser) %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
            avahi_simple_poll_quit(simple_poll);
            return;

        case AVAHI_BROWSER_NEW:
//            fprintf(stderr, "(Browser) NEW: service '%s' of type '%s' in domain '%s'\n", name, type, domain);

            /* We ignore the returned resolver object. In the callback
               function we free it. If the server is terminated before
               the callback function is called the server will free
               the resolver for us. */

            if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, resolve_callback, L))) {
//                fprintf(stderr, "Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(c)));
            }
            break;

        case AVAHI_BROWSER_REMOVE:
//            fprintf(stderr, "(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
            // remove from our local cache
            
            lua_getfield(L, LUA_ENVIRONINDEX, "found.table");
            lua_pushnil(L);
            
            char* key = malloc(strlen(name)+strlen(type)+strlen(domain)+2);
            sprintf(key, "%s:%s:%s", name, type, domain);
            lua_setfield(L, -2, key);
            free(key);

            break;

        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
//            fprintf(stderr, "(Browser) %s\n", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
            break;
    }
}

static void client_callback(AvahiClient *c, AvahiClientState state, void * userdata) {
    assert(c);
    assert(userdata);
    AvahiSimplePoll *simple_poll = userdata;

    /* Called whenever the client or server state changes */

    if (state == AVAHI_CLIENT_FAILURE) {
//        fprintf(stderr, "Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
        avahi_simple_poll_quit(simple_poll);
    }
}


//------------------------------------------------------------------------------
// Module Management
//------------------------------------------------------------------------------

LUALIB_API int avahi_version(lua_State *L){

    lua_pushstring(L,  "lib avahi version:" AVAHI_LUA_VERSION ", authors: " AVAHI_LUA_AUTHORS );
    return 1;
}

//------------------------------------------------------------------------------

LUALIB_API int avahi_help(lua_State *L){
    lua_pushstring(L, "usage:\n"

      // TODO
      "help() - return this message\n"
      "version() - return version string\n"
      "open() - create and initialize the client\n"
      "poll(sleep_ms) - poll the client to give time to the browsers\n"
      "browse(servicetype) - create and return a browser object that will look for services matching service type\n"
      "  browser:status() - return the status of the browser\n"
      "  browser:found() - return a table of all found clients\n"
      "close() - free the client, call init() again to start over\n"
      );
    return 1;
}

//------------------------------------------------------------------------------

/* init function, will be called when lua run require */
LUALIB_API int luaopen_avahi (lua_State *L) {
    lua_newtable(L);
    lua_replace(L, LUA_ENVIRONINDEX);
    luaL_register(L, "avahi", avahi_functions);

    return 1;
}

//------------------------------------------------------------------------------

LUALIB_API int avahi_open(lua_State *L) {

  AvahiSimplePoll *simple_poll = NULL;

  AvahiClient *client = NULL;

//  fprintf(stderr, "entering avahi_open\n");

  simple_poll = avahi_simple_poll_new();
  
  /* Allocate main loop object */
  if (!simple_poll) {
      return luaL_error(L, "Failed to create simple poll object."); 
  }
  
//  fprintf(stderr, "created simple poll object\n");

  AvahiSimplePoll** simple_poll_ud = (AvahiSimplePoll**)lua_newuserdata(L, sizeof(AvahiSimplePoll *));
  
  *simple_poll_ud = simple_poll;
  
  lua_setfield(L, LUA_ENVIRONINDEX, "avahi.simplepoll");

  /* Allocate a new client */
  int error;

//  fprintf(stderr, "getting ap\n");

  const AvahiPoll* ap = avahi_simple_poll_get(simple_poll);
  
//  fprintf(stderr, "creating client with ap: %x\n", (int)ap);

  client = avahi_client_new(ap, 0, client_callback, (void*)simple_poll, &error);

  /* Check whether creating the client object succeeded */
  if (!client) {
      return luaL_error(L, "Failed to create client: %s", avahi_strerror(error)); 
  }
//  fprintf(stderr, "created client object\n");

  AvahiClient**client_ud = (AvahiClient**)lua_newuserdata(L, sizeof(AvahiClient *));
  
  *client_ud = client;
  
  lua_setfield(L, LUA_ENVIRONINDEX, "avahi.client");

  return 0;
}

//------------------------------------------------------------------------------

LUALIB_API int avahi_close(lua_State *L){

  AvahiServiceBrowser *browser = get_environ_ud(L, "avahi.browser"); 
  assert(browser);
  avahi_service_browser_free(browser);

  // remove the entry from the table
  lua_pushnil(L);
  lua_setfield(L, LUA_ENVIRONINDEX,  "avahi.browser");

  AvahiClient *client = get_environ_ud(L,  "avahi.client");

  if (client) {
    avahi_client_free(client);
  }

  // remove the entry from the table
  lua_pushnil(L);
  lua_setfield(L, LUA_ENVIRONINDEX,  "avahi.client");

  AvahiSimplePoll *simple_poll = get_environ_ud(L, "avahi.simplepoll");

  if (simple_poll) {
    avahi_simple_poll_free(simple_poll);
  }

  // remove the entry from the table
  lua_pushnil(L);
  lua_setfield(L, LUA_ENVIRONINDEX,  "avahi.simplepoll");
  return 0;
}


//------------------------------------------------------------------------------

LUALIB_API int avahi_poll(lua_State *L){

  int params = lua_gettop(L);
  int sleep_time = 0; // in ms
  
  if (params > 0) {
    if (lua_isnumber(L,1)) {
      sleep_time = (double)luaL_checknumber(L, 1) * 1000;  // convert from ms to seconds
    } else {
      return luaL_argerror (L, 1, "expecting sleep time in seconds");
    }
  }
  
  AvahiSimplePoll *simple_poll = get_environ_ud(L, "avahi.simplepoll");

  int poll_result = avahi_simple_poll_iterate	(	simple_poll, sleep_time);	

  if (poll_result < 0) {
    lua_pushinteger(L, poll_result);
    return 1;
  } else if (poll_result > 0) {
    lua_pushinteger(L, poll_result);
    return 1;
  } else {
    return 0;
  }
}

//------------------------------------------------------------------------------

LUALIB_API int avahi_browse(lua_State *L){
   
  int n = lua_gettop(L);  // Number of arguments
  if (n != 1) 
      return luaL_error(L, "Got %d arguments expected 1 (service type)", n); 

  const char* servicetype = luaL_checkstring (L, 1);      

  // Allocate memory for a pointer to to object
  AvahiServiceBrowser **browser = (AvahiServiceBrowser **)lua_newuserdata(L, sizeof(AvahiServiceBrowser *));  
  
  AvahiClient *client = get_environ_ud(L, "avahi.client");

  *browser = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, servicetype, NULL, 0, browse_callback, L);

  if (!*browser) {
    return  luaL_error(L, "Failed to create new browser"); 
  } 

  lua_setfield(L, LUA_ENVIRONINDEX, "avahi.browser");


  // create an empty "found" table
  lua_newtable(L);
  lua_setfield(L, LUA_ENVIRONINDEX, "found.table");
  
  return 0; 
}

//------------------------------------------------------------------------------

LUALIB_API int avahi_found(lua_State *L){

  lua_getfield(L, LUA_ENVIRONINDEX, "found.table");
  
  return 1;
}

//------------------------------------------------------------------------------

LUALIB_API int avahi_status(lua_State *L){

  lua_getfield(L, LUA_ENVIRONINDEX, "status.string");

  return 1;
}
