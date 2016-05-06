/**
 * \file supervisor.h
 * \brief Structures and functions for supervisor.
 * \author Marek Svepes <svepemar@fit.cvut.cz>
 * \date 2013
 * \date 2014
 */
/*
 * Copyright (C) 2013,2014 CESNET
 *
 * LICENSE TERMS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <inttypes.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>

#include <libtrap/trap.h>
#include "config.h"


#ifndef PERM_LOGSDIR
#define PERM_LOGSDIR   (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH) ///< Permissions of directory with stdout and stderr logs of modules
#endif

#ifndef PERM_LOGFILE
#define PERM_LOGFILE   (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH) ///< Permissions of files with stdout and stderr logs of module
#endif

#define RUNNING_MODULES_ARRAY_START_SIZE   10 ///< Initial size of allocated running_modules array.
#define IFCES_ARRAY_START_SIZE   5 ///< Initial size of allocated interface_t array of every module.

#define IN_MODULE_IFC_DIRECTION   1  ///< Constant for input module interface direction
#define OUT_MODULE_IFC_DIRECTION   2  ///< Constant for output module interface direction
#define SERVICE_MODULE_IFC_DIRECTION   3  ///< Constant for service module interface direction

#define TCP_MODULE_IFC_TYPE   1  ///< Constant for tcp module interface type
#define UNIXSOCKET_MODULE_IFC_TYPE   2  ///< Constant for unixsocket module interface type
#define SERVICE_MODULE_IFC_TYPE   3  ///< Constant for service module interface type
#define FILE_MODULE_IFC_TYPE   4 ///< Constant for file module interface type
#define BLACKHOLE_MODULE_IFC_TYPE   5 ///<  Constant for blackhole module interface type

#define INVALID_MODULE_IFC_ATTR   -1  ///< Constant for invalid module interface attribute


/**
 * Version of supervisor
 *
 * Used from config.h that is generated by the configure script.
 */
const char sup_package_version[] __attribute__((used)) = PACKAGE_VERSION;

/**
 * Git revision of supervisor
 *
 * Used from config.h that is generated by the configure script.
 */
const char sup_git_version[] __attribute__((used)) = GIT_VERSION;

/***********STRUCTURES***********/
typedef struct str_lst_s str_lst_t;

/* Structure used as a linked list of strings */
struct str_lst_s {
   char *str;
   str_lst_t *next;
};

typedef struct in_ifc_stats_s {
   uint64_t recv_msg_cnt;
   uint64_t recv_buffer_cnt;
} in_ifc_stats_t;

typedef struct out_ifc_stats_s {
   uint64_t sent_msg_cnt;
   uint64_t dropped_msg_cnt;
   uint64_t sent_buffer_cnt;
   uint64_t autoflush_cnt;
} out_ifc_stats_t;


/** Structure with information about one loaded interface of module */
typedef struct interface_s {
   char *ifc_note; ///< Interface note
   char *ifc_type; ///< Interface type (TCP / UNIXSOCKET / SERVICE)
   char *ifc_params; ///< Interface parameters (input interface ~ address, port; output interface ~ port, number of connections; service interface ~ port, number of connections)
   char *ifc_direction; ///< Interface direction (IN / OUT / SERVICE)
   int int_ifc_direction; ///< Integer value of interface direction - for faster comparison
   int int_ifc_type; ///< Integer value of interface type - for faster comparison
} interface_t;

typedef struct modules_profile_s modules_profile_t;

struct modules_profile_s {
   char *profile_name;
   int profile_enabled;
   modules_profile_t *next;
};

/** Structure with information about one running module */
typedef struct running_module_s {
   uint32_t total_in_ifces_cnt;  ///< Number of all trap input interfaces the module is running with - received via service interface
   uint32_t total_out_ifces_cnt;  ///< Number of all trap output interfaces the module is running with - received via service interface

   interface_t *config_ifces;  ///< Array of interfaces loaded from the configuration file (these ifces are passed via "-i" parameter to the executed module).
   uint32_t config_ifces_cnt;  ///< Number of interfaces loaded from the configuration file.
   uint32_t config_ifces_arr_size;  ///< Size of allocated array for interfaces loaded from the configuration file (array "config_ifces").

   in_ifc_stats_t *in_ifces_data;  ///< Contains statistics about all input interfaces the module is running with (size of total_in_ifces_cnt)
   out_ifc_stats_t *out_ifces_data;  ///< Contains statistics about all output interfaces the module is running with (size of total_out_ifces_cnt)

   int module_enabled; ///< TRUE if module is enabled, else FALSE.   /*** RELOAD ***/
   char *module_name; ///< Module name (loaded from config file).   /*** RELOAD ***/
   char *module_params; ///< Module parameter (loaded from config file).   /*** RELOAD ***/
   char *module_path; ///< Path to module from current directory   /*** RELOAD ***/


   int module_served_by_service_thread; ///< TRUE if module was added to graph struct by sevice thread, FALSE on start.   /*** RELOAD ***/
   uint8_t module_modified_by_reload; ///< Variable used during reload_configuration, TRUE if already loaded module is changed by reload, else FALSE
   uint8_t module_checked_by_reload; ///< Variable used during reload_configuration, TRUE if a new module is added or already loaded module is checked (used for excluding modules with non-unique name)
   modules_profile_t *modules_profile;   /*** RELOAD ***/
   int module_is_my_child;   /*** RELOAD ***/
   int remove_module;   /*** RELOAD ***/
   int init_module;   /*** RELOAD ***/

   int module_status; ///< Module status (TRUE ~ running, FALSE ~ stopped)   /*** SERVICE ***/
   int module_running; ///< TRUE after first start of module, else FALSE.   /*** RELOAD/ALLOCATION ***/
   int module_restart_cnt; ///< Number of module restarts.   /*** INIT ***/
   int module_restart_timer;  ///< Timer used for monitoring max number of restarts/minute.   /*** INIT ***/
   int module_max_restarts_per_minute;   /*** RELOAD ***/
   pid_t module_pid; ///< Modules process PID.   /*** RELOAD/START ***/
   int sent_sigint;   /*** INIT ***/

   unsigned int virtual_memory_usage;   /*** INIT ***/

   long int total_cpu_usage_during_module_startup;   /*** INIT ***/
   int last_period_cpu_usage_kernel_mode; ///< Percentage of CPU usage in last period in kernel mode.   /*** INIT ***/
   int last_period_cpu_usage_user_mode; ///< Percentage of CPU usage in last period in user mode.   /*** INIT ***/
   int last_period_percent_cpu_usage_kernel_mode; ///< Percentage of CPU usage in current period in kernel mode.   /*** INIT ***/
   int last_period_percent_cpu_usage_user_mode; ///< Percentage of CPU usage in current period in user mode.   /*** INIT ***/
   int overall_percent_module_cpu_usage_kernel_mode;   /*** INIT ***/
   int overall_percent_module_cpu_usage_user_mode;   /*** INIT ***/

   int module_service_sd; ///< Socket descriptor of the service connection.   /*** INIT ***/
   int module_service_ifc_isconnected; ///< if supervisor is connected to module ~ TRUE, else ~ FALSE   /*** INIT ***/
   int module_service_ifc_conn_attempts; // Count of supervisor's connection attempts to module's service interface    /*** INIT ***/
   int module_service_ifc_conn_fails;   /*** INIT ***/
   int module_service_ifc_conn_block;   /*** INIT ***/
   int module_service_ifc_timer;   /*** INIT ***/
} running_module_t;




typedef struct sup_client_s {
   FILE *client_input_stream;
   FILE *client_output_stream;
   int client_input_stream_fd;
   int client_sd;
   int client_connected;
   int client_id;
   pthread_t client_thread_id;
} sup_client_t;


typedef struct server_internals_s {
   sup_client_t **clients;
   int clients_cnt;
   int server_sd;
   int daemon_terminated;
   uint16_t next_client_id;
   int config_mode_active;
   pthread_mutex_t lock;
} server_internals_t;


typedef struct reload_config_vars_s {
   xmlDocPtr doc_tree_ptr;
   xmlNodePtr root_node;
   xmlNodePtr current_node;
   xmlNodePtr module_elem;
   xmlNodePtr module_atr_elem;
   xmlNodePtr ifc_elem;
   xmlNodePtr ifc_atr_elem;
   int current_module_idx;
   int new_module;
   int module_ifc_insert;
   int inserted_modules;
   int removed_modules;
   int modified_modules;
} reload_config_vars_t;

union tcpip_socket_addr {
   struct addrinfo tcpip_addr; ///< used for TCPIP socket
   struct sockaddr_un unix_addr; ///< used for path of UNIX socket
};

typedef struct service_msg_header_s {
   uint8_t com;
   uint32_t data_size;
} service_msg_header_t;

/***********FUNCTIONS***********/

/**
 * \defgroup GROUP_NAME short_description
 *
 * TODO group description.
 * @{
 */

/**
 *
 */
char *get_absolute_file_path(char *file_name);
char *create_backup_file_path();
void create_shutdown_info(char **backup_file_path);
void print_xmlDoc_to_stream(xmlDocPtr doc_ptr, FILE *stream);
char *get_formatted_time();

/**
 * Parsing function for modules "params" element from the configuration file
 * that it is used by prepare_module_args() function.
 *
 * The parameter delimiter is a white-space ' ' but if the parameter is quoted,
 * white-spaces are part of the parameter. If the quotes have to be passed as a part of the parameter,
 * apostrophes can be used.
 *
 * Example of the params element in the config file:
 * <params>-o param_without_white-spaces "param with white-spaces" 'param with "quotes" and white-spaces'</params>
 *
 * Arguments passed to binary:
 * 1) -o
 * 2) param_without_white-spaces
 * 3) param with white-spaces
 * 4) param with "quotes" and white-spaces
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 * @param[out] params_num In case of success it contains number of parsed args, otherwise 0.
 * @return In case of success array of strings (some of the arguments for the module binary) is returned, otherwise NULL.
 */
char **parse_module_params(const uint32_t module_idx, uint32_t *params_num);

/**
 * Function prepares an array of strings which is passed to execvp() function by service_start_module() function.
 * The array contains all needed arguments for the module binary. If the module has trap interfaces, "-i" arg is added
 * and interfaces specifier is generated (e.g. "t:1234,u:sock,s:service_sock"). If the module has a non-empty
 * "params", parse_module_params() function is used to add all module parameters.
 *
 * Example of the output array (8 strings plus last one terminating null pointer):
 * "module_name" "parameter1" "-o" "parameter2" "-s" "parameter3" "-i" "t:1234,u:sock,s:service_sock" NULL
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 * @return In case of success array of strings (arguments for the module binary) is returned, otherwise NULL.
 */
char **prep_module_args(const uint32_t module_idx);

int get_number_from_input_choosing_option();
int parse_numbers_user_selection(int **array);
void init_module_variables(int module_number);
char *get_param_by_delimiter(const char *source, char **dest, const char delimiter);
void print_statistics();
void print_statistics_legend();
char *make_formated_statistics(uint8_t stats_mask);
int find_loaded_module(char *name);
void generate_backup_config_file();
/**@}*/



/**
 * \defgroup GROUP_NAME short_description
 *
 * TODO group description.
 * @{
 */

/**
 *
 */
void *netconf_server_routine_thread(void *arg);
int netconf_supervisor_initialization(xmlNodePtr *running);
xmlDocPtr netconf_get_state_data();
/**@}*/



 /**
 * \defgroup GROUP_NAME short_description
 *
 * TODO group description.
 * @{
 */

/**
 *
 */
void reload_process_supervisor_element(reload_config_vars_t **config_vars);
void reload_process_module_atribute(reload_config_vars_t **config_vars, char **module_ifc_atr);
int reload_process_module_interface_atribute(reload_config_vars_t **config_vars, char **module_ifc_atr);
void reload_check_modules_interfaces_count(reload_config_vars_t  **config_vars);
int reload_find_and_check_module_basic_elements(reload_config_vars_t **config_vars);
int reload_find_and_check_modules_profile_basic_elements(reload_config_vars_t **config_vars);
void reload_count_module_interfaces(reload_config_vars_t **config_vars);
void reload_check_module_allocated_interfaces(const int running_module_idx, const int ifc_cnt);
void check_running_modules_allocated_memory();
void reload_resolve_module_enabled(reload_config_vars_t **config_vars);
int reload_configuration(const int choice, xmlNodePtr *node);
/**@}*/



 /**
 * \defgroup startup_functions Functions used by supervisor during startup
 *
 * @{
 */

/**
 * Function tries to create a directory with path defined by logs_path variable
 * (logs_path is set by user in the configuration file or by -L program parameter).
 * If it fails (because of access rights etc.) or the variable wasn't set, it uses default
 * path according to the supervisor mode - {DAEMON,INTERACTIVE,NETCONF}_DEFAULT_LOGSDIR_PATH.
 *
 * @return CREATED_DEFAULT_LOGS or CREATED_USER_DEFINED_LOGS in case of success, otherwise -1.
 */
/**
 * Function opens all needed file streams for supervisor log files
 * (separated files for supervisor log, debug log, module events and module statistics).
 */
int init_paths();

/**
 * Supervisor signal handler catches following signals:
 * SIGINT, SIGQUIT - after these signals, it let modules run and it generates backup file
 * SIGTERM - after termination signal, it stops all modules and does not generate backup file
 * SIGSEGV - in case of segmentation fault, it let modules run and generates backup file
 */
void sup_sig_handler(int catched_signal);

/**
 * Whole program initialization - it creates logs directory, log files, allocates needed structures,
 * loads initial configuration from the configuration file, starts service thread and registers signal handler.
 *
 * @return 0 in case of success, otherwise -1.
 */
int supervisor_initialization();

/**
 * Creates a new thread doing service thread routine.
 *
 * @return 0 in case of success, otherwise number of error that occurred during pthread_create.
 */
int start_service_thread();

/**
 * Function parses program arguments using SUP_GETOPT macro (it is set by configure script to getopt
 * or getopt_long function according to the available system libraries).
 *
 * @param[in] argc Argument counter passed from the main function.
 * @param[in] argv Argument values passed from the main function.
 * @return DAEMON_MODE_CODE or INTERACTIVE_MODE_CODE in case of success, otherwise -1.
 */
int parse_prog_args(int *argc, char **argv);
/**@}*/



 /**
 * \defgroup cleanup_functions Supervisor clean up (and termination) functions
 *
 * @{
 */

/**
 * Frees dynamically allocated memory of the specified module (all structures, strings etc.).
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 */
void free_module_on_index(const int module_idx);

/**
 * Frees dynamically allocated memory of the specified module - only modules interfaces.
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 */
void free_module_interfaces_on_index(const int module_idx);

/**
 * Closes all opened file streams (supervisor logs) and frees output file path strings.
 */
void free_output_file_strings_and_streams();

/**
 * Frees dynamically allocated memory of the specified module and shifts whole array of the loaded modules.
 * (loaded modules A B C -> free B -> result A C)
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 */
void free_module_and_shift_array(const int module_idx);

/**
 * Terminating function of the program. According to the function parameters it stops running modules
 * and generates a backup file (in case of crash or if some module is still running). After that it stops service thread,
 * in daemon mode it waits for all clients threads and finally it does all needed clean up.
 *
 * @param[in] stop_all_modules Flag defining whether it stops running modules or not.
 * @param[in] generate_backup Flag defining whether the backup file is generated or not.
 */
void supervisor_termination(const uint8_t stop_all_modules, const uint8_t generate_backup);
/**@}*/


 /**
 * \defgroup user_menu_options Functions performing operations shown in the user menu
 *
 * @{
 */

/**
 * Prints out loaded modules grouped by their profile (if they have any) and all information about them
 * (starting with its name, status, parameters and interfaces - including interfaces parameters).
 */
void interactive_show_available_modules();

/**
 * Prints out menu with options (start/stop module, start/stop configuration, show status etc.)
 * and loads one number with chosen option by user.
 *
 * @return 1 to 9 if success, otherwise -1.
 */
int interactive_get_option();

/**
 * All loaded modules are enabled (they will be automatically started by service thread).
 */
void interactive_start_configuration();

/**
 * All loaded modules are disabled (they will be automatically stopped by service thread).
 */
void interactive_stop_configuration();

/**
 * Prints out running modules grouped by their profile. After that, user can type in numbers of modules to restart.
 * (selected modules will be disabled and automatically stopped -> enabled -> started by service thread)
 */
void interactive_restart_module();

/**
 * Prints out stopped modules grouped by their profile. After that, user can type in numbers of modules to enable.
 * (enabled modules are automatically started by service thread)
 */
void interactive_set_enabled();

/**
 * Prints out running modules grouped by their profile. After that, user can type in numbers of modules to disable.
 * (disabled modules are automatically stopped by service thread)
 */
void interactive_set_disabled();

/**
 * Prints out loaded modules grouped by their profile (if they have any)
 * and their status (running or stopped) and their PID (process id).
 */
void interactive_show_running_modules_status();

/**
 * Prints out important information about current program
 * (package version, git version, startup date, config file path, logs directory path etc.)
 */
void interactive_print_supervisor_info();
/**@}*/


/**
 * \defgroup service_functions Functions used by service thread
 *
 * These functions are used in service thread routine which takes care of modules
 * status (enabled modules has to run and disabled modules has to be stopped).
 * Besides, it connects to modules with service interface and receives their statistics
 * in JSON format which are decoded and saved to modules structures.
 * @{
 */

/**
 * Function checks the status of every module (whether it is running or not).
 *
 * @return Returns a number of running modules (a non-negative number).
 */
int service_check_modules_status();

/**
 * Function starts stopped modules which are enabled (they should run).
 * Part of this function is also restart limit checking of every module which is being stared.
 */
void service_update_modules_status();

/**
 * Creates a new process and executes modules binary with all needed parameters.
 * It also redirects stdout and stderr of the new process.
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 */
void service_start_module(const int module_idx);

/**
 * Function cleans up after finished children processes of stopped modules.
 */
void service_clean_after_children();

/**
 * Function tries to stop running modules which are disabled using SIGINT signal.
 */
void service_stop_modules_sigint();

/**
 * Function stops running modules which are disabled using SIGKILL signal.
 * Used only if the module does not responds to SIGINT signal.
 */
void service_stop_modules_sigkill();

/**
 * Function checks connections between supervisor and service interface of every module.
 * It also checks number of errors during the connection with every module and if the limit of errors is reached,
 * it blocks the connection.
 */
void service_check_connections();

/**
 * Connects to service interface of the specified module.
 * It also checks number of connection attempts and if the limit is reached, connection is blocked.
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 */
void service_connect_to_module(const int module_idx);

/**
 * Closes the connection with service interface of the specified module.
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 */
void service_disconnect_from_module(const int module_idx);

/**
 * Function receives the data from a service interface of the specified module and saves it to the output parameter data.
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 * @param[in] size Number of bytes to receive.
 * @param[out] data Already allocated memory used for saving the data.
 * @return Returns 0 if success, otherwise -1.
 */
int service_recv_data(int module_idx, uint32_t size, void **data);

/**
 * Function sends the data to a service interface of the specified module.
 *
 * @param[in] module_idx Index to array of modules (array of structures).
 * @param[in] size Number of bytes to send.
 * @param[in] data Memory with the data.
 * @return Returns 0 if success, otherwise -1.
 */
int service_send_data(int module_idx, uint32_t size, void **data);

/**
 * Service thread routine is periodically checking and updating modules status
 * and connection with the modules. It also receives statistics from modules service interface.
 */
void *service_thread_routine(void *arg __attribute__ ((unused)));

/**
 * Function decodes the data in JSON format and saves the statistics to specified modules structure.
 *
 * @param[in] data Memory with the data in JSON format.
 * @param[in] module_idx Index to array of modules (array of structures).
 * @return Returns 0 if success, otherwise -1.
 */
int service_decode_module_stats(char **data, int module_idx);
/**@}*/



/**
 * \defgroup GROUP_NAME short_description
 *
 * TODO group description.
 * @{
 */

/**
 *
 */
long int get_total_cpu_usage();
void update_module_cpu_usage();
void update_module_mem_usage();
/**@}*/



/**
 * \defgroup daemon_functions Supervisor daemon mode functions
 *
 * Set of functions used by supervisor while running as a daemon process.
 * It behaves as a server and serves incoming supervisor clients and their requests.
 * The request can be: reloading configuration (reloading the configuration file),
 * receiving modules statistics (their interfaces counters) or changing configuration
 * (start / stop module, check modules status etc.).
 *
 * @{
 */

/**
 * Creates a new process and disconnects it from terminal (daemon process).
 *
 * @return Returns 0 if success, otherwise -1.
 */
int daemon_init_process();

/**
 * Allocates needed structures and variables (clients array etc.)
 *
 * @return Returns 0 if success, otherwise -1.
 */
int daemon_init_structures();

/**
 * Creates and binds a socket for incoming connections.
 *
 * @return Returns 0 if success, otherwise -1.
 */
int daemon_init_socket();

/**
 * Function initializes daemon process, structures and socket using daemon_init_{process,structures,socket} functions.
 *
 * @return Returns 0 if success, otherwise -1.
 */
int daemon_mode_initialization();

/**
 * Server routine for daemon process (acceptor thread).
 * It accepts a new incoming connection and starts a serving thread (if there is a space for another client).
 */
void daemon_mode_server_routine();

/**
 * Tries to receive initial data from a new client and reads a mode code.
 *
 * @param[in] cli Structure with clients private data.
 * @return If success, it returns mode code (client wants to configure, reload configuration or receive stats about modules).
 * Otherwise it returns negative value (-3 timeout, -2 client disconnection, -1 another error).
 */
int daemon_get_code_from_client(sup_client_t **cli);

/**
 * Function sends options to a client during configuration mode (a menu with options the client can choose from - start or stop module etc.).
 */
void daemon_send_options_to_client();

/**
 * Function opens input and output streams for a new client.
 *
 * @param[in] cli Structure with clients private data.
 * @return Returns 0 if success, otherwise -1.
 */
int daemon_open_client_streams(sup_client_t **cli);

/**
 * Function disconnects a client and makes needed clean up (closing streams etc.).
 *
 * @param[in] cli Structure with clients private data.
 */
void daemon_disconnect_client(sup_client_t *cli);

/**
 * Daemons routine for serving new incoming clients (every client gets a thread doing this routine).
 * This routine opens clients streams, receives a mode code from the client and according to the the received mode code
 * it performs required operations.
 *
 * @param[in] cli Structure with clients private data.
 */
void *daemon_serve_client_routine (void *cli);
/**@}*/

#endif
