#ifndef MODULE_CONTROL_H
#define MODULE_CONTROL_H

#include "module.h"

/*--BEGIN superglobal symbolic const--*/
/*--END superglobal symbolic const--*/

/*--BEGIN macros--*/

/**
 * @details Time in micro seconds between sending SIGINT and SIGKILL to running
 *  modules_ll. Service thread sends SIGINT to stop running module, after time
 *  defined by this constant it checks modules_ll status and if the module is still
 *  running, service thread sends SIGKILL to stop it.
 */
#define WAIT_FOR_INSTS_TO_HANDLE_SIGINT 500000

/*--END macros--*/

/*--BEGIN superglobal typedef--*/
/*--END superglobal typedef--*/

/*--BEGIN superglobal vars--*/
// denote with extern
//extern sr_conn_ctx_t *sr_con = NULL;
/*--END superglobal vars--*/

/*--BEGIN superglobal fn prototypes--*/

/**
 * @brief Checks and assigns running status of each instance
 * @return number of running instances
 * */
extern uint32_t get_running_insts_cnt();

/**
 * @brief Checks and assigns running status of single instance
 * */
extern void instance_set_running_status(instance_t *inst);

/**
 * @brief Sends SIGINT signal to all modules that TODO ...
 * @param module_ll_head First node of linked list of modules to signal
 * */
extern void insts_stop_sigint();

/**
 * @brief Sends SIGKILL signal to all modules that TODO ...
 * @param module_ll_head First node of linked list of modules to signal
 * */
extern void insts_stop_sigkill();

/**
 * @brief Stops all modules of given group
 * @details First it's using stop_modules_sigint, then stop_modules_sigkill and
 *  finally it cleans all the structures of modules from given group and the group
 *  itself.
 * @param name Name of module group whose modules should be stopped
 * */
extern void module_group_stop_remove_by_name(const char *name);


/**
 * TODO
 * */
extern void module_stop_remove_by_name(const char *name);

/**
 * @brief Stops module of given name
 * @details First it's using stop_modules_sigint, then stop_modules_sigkill and
 *  finally it cleans the module structure.
 * @param name Name of module to stop
 * @param remove Whether to remove module from vector of configured modules
 * */
extern void instance_stop_remove_by_name(const char *name);
/*--END superglobal fn prototypes--*/

#endif
