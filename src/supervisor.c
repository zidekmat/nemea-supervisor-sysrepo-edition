#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <sys/un.h>
#include <sysrepo.h>
#include <sysrepo/trees.h>
#include <libtrap/trap.h>
#include <sysrepo/values.h>

#include "supervisor.h"
#include "module_control.h"
#include "conf.h"
#include "run_changes.h"
#include "stats.h"

/****************************************************************************/
 /*--BEGIN superglobal vars--*/
#ifndef TEST
const char *ns_root_sr_path = "/nemea:nemea-supervisor";
#endif
 /*--END superglobal vars--*/
 /****************************************************************************/

 /****************************************************************************/
 /*--BEGIN local #define--*/
#define PROGRAM_IDENTIFIER_FSR "nemea-supervisor" ///< Program identifier supplied to
                                                  ///<  sysrepo

#define CHECK_DIR 1
#define CHECK_FILE 2



/*
 * Time in micro seconds for which the service thread spends sleeping after each period.
 * The period means all tasks service thread has to complete - restart/stop instances
 * according to their enable flag, receive their statistics etc.
 */
#define SERVICE_THREAD_SLEEP_IN_MICSEC 1500000

//
#define SERVICE_WAIT_BEFORE_TIMEOUT 25000

#define NUM_SERVICE_IFC_PERIOD 30 ///< Defines number of supervisor_routine loops
                                   ///<  to take before reconnecting again


///< Permissions of directory with stdout and stderr logs of instances
#define PERM_LOGSDIR   (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | \
                        S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH)

#define VERBOSE_STATISTICS_LEGEND VERBOSE(STATISTICS,"Legend for an interface statistics:\n"\
                        "\tCNT_RM - counter of received messages  on the input interface\n"\
                        "\tCNT_RB - counter of received buffers on the input interface\n"\
                        "\tCNT_SM - counter of sent messages on the output interface\n"\
                        "\tCNT_SB - counter of sent buffers on the output interface\n"\
                        "\tCNT_DM - counter of dropped messages on the output interface\n"\
                        "\tCNT_AF - autoflush counter of the output interface\n"\
                        "Statistics example:\n"\
                        "\tmodule_name,interface_direction,interface_number,stats\n"\
                        "\tmodule,in,number,CNT_RM,CNT_RB\n"\
                        "\tmodule,out,number,CNT_SM,CNT_SB,CNT_DM,CNT_AF\n"\
                        "--------------------------------------------------------")

 /*--END local #define--*/
 /****************************************************************************/

 /****************************************************************************/
 /*--BEGIN local typedef--*/

typedef struct sr_conn_link_s {
 sr_conn_ctx_t *conn; ///< Sysrepo connection to use in application
 sr_session_ctx_t *sess; ///< Sysrepo session to use in application
 sr_subscription_ctx_t *subscr;
} sr_conn_link_t;

typedef enum service_msg_header_type_e {
   SERVICE_GET_COM = 10,
   SERVICE_SET_COM = 11,
   SERVICE_OK_REPLY = 12
} service_msg_header_type_t;

typedef union tcpip_socket_addr_u {
   struct addrinfo tcpip_sa; ///< used for TCPIP socket
   struct sockaddr_un unix_sa; ///< used for path of UNIX socket
} tcpip_socket_addr_t;

typedef struct service_msg_header_s {
   service_msg_header_type_t com;
   uint32_t data_size;
} service_msg_header_t;

 /*--END local typedef--*/
 /****************************************************************************/

 /****************************************************************************/
 /* --BEGIN local vars-- */
// TODO do structu???
bool supervisor_stopped = false; ///< Controls loop of supervisor_routine
bool supervisor_initialized = false; ///< Specifies whether supervisor_initialization
                                     ///<  completed successfully
bool terminate_insts_at_exit = false; ///< Specifies whether signal handler wants
                                        ///<  to terminate all instances at exit
int supervisor_exit_code = EXIT_SUCCESS; ///< What exit code to return at exit
uint64_t last_total_cpu = 0; // Variable with total cpu usage of whole operating system


time_t sup_init_time = 0; // Time of supervisor initialization
sr_conn_link_t sr_conn_link = {
      .conn = NULL,
      .sess = NULL,
}; ///< Sysrepo connection link
 /* --END local vars-- */
 /****************************************************************************/

 /****************************************************************************/
 /* --BEGIN full fns prototypes-- */

static int check_file_type_perm(char *item_path, uint8_t file_type, int file_perm);
static char *get_absolute_file_path(char *file_name);

static void free_output_file_strings_and_streams();
static int load_configuration();
static void sig_handler(int catched_signal);
static void insts_start();


static void insts_update_resources_usage();
static int get_total_cpu_usage(uint64_t *total_cpu_usage);

static inline void insts_terminate_all();

// fetching stats + communication
static inline void inst_get_stats(instance_t *inst);
static inline void inst_get_vmrss(instance_t *inst);
static inline void check_insts_connections();
static inline void connect_to_inst(instance_t *inst);
static inline void disconnect_from_inst(instance_t *inst);
static inline void get_service_ifces_stats();
static int send_ifc_stats_request(const instance_t *inst);
inline static int recv_ifc_stats(instance_t *inst);
static int service_recv_data(instance_t *inst, uint32_t size, void *data);
static int service_decode_inst_stats(char *data, instance_t *inst);

/**
 * @brief Start single instance process
 * @param inst Instance to start
 * */
static void inst_start(instance_t *inst);

/**
 * @brief Saves PIDs of running instances to sysrepo so that they can be
 *  recovered after supervisor restart.
 * */
static void insts_save_running_pids();

// TODO debug, really needed? nope
static char *make_formated_statistics(uint8_t stats_mask);
static inline void print_statistics();
// END TO.DO above
 /* --END full fns prototypes-- */
 /****************************************************************************/

 /****************************************************************************/
 /* --BEGIN superglobal fns-- */
int init_paths()
{
   char *ptr = NULL;
   char *buffer = NULL;
   //char modules_logs_path[PATH_MAX];
   char path[PATH_MAX];

   /* Check logs directory */
   if (check_file_type_perm(logs_path, CHECK_DIR, R_OK | W_OK) == -1) {
      PRINT_ERR("Check the path and permissions (read and write needed) of the logs directory \"%s\".", logs_path)
      return -1;
   }

   // TODO check socket path
   // TODO do I need sockets? no those are for interactive mode

   /* create absolute paths */

   ptr = get_absolute_file_path(logs_path);
   if (ptr != NULL) {
      NULLP_TEST_AND_FREE(logs_path);
      if (ptr[strlen(ptr) - 1] != '/') {
         logs_path = (char *) calloc(strlen(ptr) + 2, sizeof(char));
         sprintf(logs_path, "%s/", ptr);
      } else {
         logs_path = strdup(ptr);
      }
   } else if (logs_path[strlen(logs_path) - 1] != '/') {
      buffer = (char *) calloc(strlen(logs_path) + 2, sizeof(char));
      sprintf(buffer, "%s/", logs_path);
      NULLP_TEST_AND_FREE(logs_path);
      logs_path = buffer;
   }

   // Create instances logs path
   memset(path, 0, PATH_MAX * sizeof(char));
   snprintf(path, PATH_MAX, "%s%s/", logs_path, INSTANCES_LOGS_DIR_NAME);
   // Try to create instances logs directory
   if (mkdir(path, PERM_LOGSDIR) == -1) {
      if (errno == EEXIST) { // path already exists
         if (check_file_type_perm(path, CHECK_DIR, R_OK | W_OK) == -1) {
            VERBOSE(N_ERR, "Check the permissions (read and write needed) of the "
                  "instances logs directory '%s'.", path)
            return -1;
         }
      } else {
         VERBOSE(N_ERR, "Could not create '%s' instances logs directory.", path)
         return -1;
      }
   }

   // Try to open supervisor debug log file
   memset(path, 0, PATH_MAX * sizeof(char));
   snprintf(path, PATH_MAX, "%s%s", logs_path, SUPERVISOR_DEBUG_LOG_FILE_NAME);
   supervisor_debug_log_fd = fopen(path, "a");
   if (supervisor_debug_log_fd == NULL) {
      VERBOSE(N_ERR, "Could not open '%s' supervisor debug log file.", path)
      return -1;
   } else {
      VERBOSE(DEBUG, "--------------------------------------------");
   }


   // Try to open statistics file
   memset(path, 0, PATH_MAX * sizeof(char));
   snprintf(path, PATH_MAX, "%s%s", logs_path, INSTANCES_STATS_FILE_NAME);
   statistics_fd = fopen(path, "a");
   if (statistics_fd == NULL) {
      VERBOSE(N_ERR, "Could not open '%s' instances statistics file.", path)
      return -1;
   } else {
      VERBOSE(STATISTICS, "--------------------------------------------");
      VERBOSE_STATISTICS_LEGEND;
   }


   // Try to open instances events logs file
   memset(path, 0, PATH_MAX * sizeof(char));
   snprintf(path, PATH_MAX, "%s%s", logs_path, INSTANCES_EVENTS_FILE_NAME);
   inst_event_fd = fopen(path, "a");
   if (inst_event_fd == NULL) {
      VERBOSE(N_ERR, "Could not open '%s' instances events log file.", path)
      return -1;
   } else {
      VERBOSE(MOD_EVNT, "--------------------------------------------")
   }


   VERBOSE(DEBUG, "daemon_flag==%d", daemon_flag)
   if (daemon_flag) {
      // Try to create main supervisor log file
      memset(path, 0, PATH_MAX * sizeof(char));
      snprintf(path, PATH_MAX, "%s%s", logs_path, SUPERVISOR_LOG_FILE_NAME);
      supervisor_log_fd = fopen (path, "a");
      if (supervisor_log_fd == NULL) {
         PRINT_ERR("Could not open \"%s\" supervisor log file.", path)
         return -1;
      } else {
         VERBOSE(SUP_LOG, "--------------------------------------------")
      }
      output_fd = supervisor_log_fd;
   } else {
      VERBOSE(DEBUG, "output_fd=stdout")
      output_fd = stdout;
   }

   return 0;
}

int supervisor_initialization()
{
   int rc;

   time(&sup_init_time);

   // Initialize main mutex
   pthread_mutex_init(&config_lock, NULL);


   // Connect to sysrepo
   rc = sr_connect(PROGRAM_IDENTIFIER_FSR, SR_CONN_DEFAULT, &sr_conn_link.conn);
   if (SR_ERR_OK != rc) {
      VERBOSE(N_ERR, "Failed to connect to sysrepo: %s", sr_strerror(rc));
      return -1;      
   }

   rc = sr_session_start(sr_conn_link.conn, SR_DS_STARTUP, SR_SESS_DEFAULT,
                         &sr_conn_link.sess);
   if (SR_ERR_OK != rc) {
       VERBOSE(N_ERR, "Failed to create sysrepo session: %s", sr_strerror(rc));
       return -1;
   }


   // Load startup configuration
   if (load_configuration() != 0) {
      return -1;
   }

   // Switch session to running datastore for following subscribtions
   rc = sr_session_switch_ds(sr_conn_link.sess, SR_DS_RUNNING);
   if (rc != SR_ERR_OK) {
      VERBOSE(N_ERR, "Failed to switch sysrepo session: %s", sr_strerror(rc))
      terminate_supervisor(false);
      return -1;
   }

   // Subscribe only to changes that are going to be applied, not attempts
   rc = sr_subtree_change_subscribe(sr_conn_link.sess,
                                    ns_root_sr_path,
                                    run_config_change_cb,
                                    NULL,
                                    0,
                                    SR_SUBSCR_DEFAULT | SR_SUBSCR_APPLY_ONLY,
                                    &sr_conn_link.subscr);
   if (rc != SR_ERR_OK) {
      VERBOSE(N_ERR, "Failed to subscribe sysrepo changes callback: %s", sr_strerror(rc))
      terminate_supervisor(false);
      return -1;
   }

   {
      char *xpath = (char *) calloc(1, sizeof(char) * (strlen(ns_root_sr_path) + 36));
      if (xpath == NULL) {
         NO_MEM_ERR
         terminate_supervisor(false);
         return -1;
      }

      sprintf(xpath, "%s/module-group/module/instance/stats", ns_root_sr_path);
      rc = sr_dp_get_items_subscribe(sr_conn_link.sess,
                                     xpath,
                                     inst_get_stats_cb,
                                     NULL,
                                     SR_SUBSCR_CTX_REUSE,
                                     &sr_conn_link.subscr);
      if (rc != SR_ERR_OK) {
         VERBOSE(N_ERR, "Failed to subscribe sysrepo instance stats callback: %s",
                 sr_strerror(rc))
         NULLP_TEST_AND_FREE(xpath)
         terminate_supervisor(false);
         return -1;
      }

      xpath = realloc(xpath, sizeof(char) * (strlen(ns_root_sr_path)) + 46);
      if (xpath == NULL) {
         NO_MEM_ERR
         terminate_supervisor(false);
         return -1;
      }
      sprintf(xpath, "%s/module-group/module/instance/interface/stats", ns_root_sr_path);

      rc = sr_dp_get_items_subscribe(sr_conn_link.sess,
                                     xpath,
                                     interface_get_stats_cb,
                                     NULL,
                                     SR_SUBSCR_CTX_REUSE,
                                     &sr_conn_link.subscr);
      if (rc != SR_ERR_OK) {
         VERBOSE(N_ERR, "Failed to subscribe sysrepo interface stats callback: %s",
                 sr_strerror(rc))
         NULLP_TEST_AND_FREE(xpath)
         terminate_supervisor(false);
         return -1;
      }

      NULLP_TEST_AND_FREE(xpath)
   }

   // Signal handling
   struct sigaction sig_action;
   sig_action.sa_handler = sig_handler;
   sig_action.sa_flags = 0;
   sigemptyset(&sig_action.sa_mask);

   if (sigaction(SIGPIPE, &sig_action, NULL) == -1) {
      VERBOSE(N_ERR, "Sigaction: signal handler won't catch SIGPIPE!")
   }
   if (sigaction(SIGINT, &sig_action, NULL) == -1) {
      VERBOSE(N_ERR, "Sigaction: signal handler won't catch SIGINT!")
   }
   if (sigaction(SIGTERM, &sig_action, NULL) == -1) {
      VERBOSE(N_ERR, "Sigaction: signal handler won't catch SIGTERM!")
   }
   if (sigaction(SIGSEGV, &sig_action, NULL) == -1) {
      VERBOSE(N_ERR, "Sigaction: signal handler won't catch SIGSEGV!")
   }
   if (sigaction(SIGQUIT,&sig_action, NULL) == -1) {
      VERBOSE(N_ERR, "action: signal handler won't catch SIGQUIT!")
   }

   supervisor_initialized = true;
   VERBOSE(DEBUG, "Supervisor successfuly initialized")
   return 0;
}

void terminate_supervisor(bool should_terminate_insts)
{
   VERBOSE(DEBUG, "Terminating supervisor")

   // Unsubscribe sysrepo callbacks first so that they won't run on empty structures
   if (sr_conn_link.subscr != NULL) {
      sr_unsubscribe(sr_conn_link.sess, sr_conn_link.subscr);
   }

   if (supervisor_initialized) {
      if (should_terminate_insts) {
         insts_terminate_all();
      } else {
         insts_save_running_pids();
      }
   }

   VERBOSE(V3, "Freeing instances vector")
   instances_free();
   VERBOSE(V3, "Freeing modules vector")
   modules_free();
   VERBOSE(V3, "Freeing groups vector")
   module_groups_free();
   VERBOSE(V3, "Freeing output strigns and streams")
   free_output_file_strings_and_streams();

   NULLP_TEST_AND_FREE(logs_path)

   VERBOSE(V3, "Freeing and disconnecting sysrepo structs")
   // Disconnect sysrepo and clean up it's connection link structure


   if (sr_conn_link.sess != NULL) {
      sr_session_stop(sr_conn_link.sess);
   }

   if (sr_conn_link.conn != NULL) {
      sr_disconnect(sr_conn_link.conn);
   }

}

 /* --END superglobal fns-- */
 /****************************************************************************/

 /****************************************************************************/
 /* --BEGIN local fns-- */

static inline void insts_terminate_all()
{
   instance_t *inst = NULL;
   pthread_mutex_lock(&config_lock);
   FOR_EACH_IN_VEC(insts_v, inst) {
      if (inst->enabled) {
         inst->enabled = false;
      }
   }

   VERBOSE(DEBUG, "killing all the flies")
   // Ensure they get killed
   insts_stop_sigint();
   usleep(WAIT_FOR_INSTS_TO_HANDLE_SIGINT);
   (void) get_running_insts_cnt();
   insts_stop_sigkill();
   pthread_mutex_unlock(&config_lock);
}

int check_file_type_perm(char *item_path, uint8_t file_type, int file_perm)
{
   struct stat st;

   if (stat(item_path, &st) == -1) {
      return -1;
   }

   if (S_ISREG(st.st_mode) == TRUE && file_type == CHECK_FILE) {
      // nothing to do here
   } else if (S_ISDIR(st.st_mode) == TRUE && file_type == CHECK_DIR) {
      // nothing to do here
   } else {
      // print warning?
      return -1;
   }

   if (access(item_path, file_perm) == -1) {
      // print warning?
      return -1;
   }

   return 0;
}

// Returns absolute path of the file / directory passed in name parameter
char *get_absolute_file_path(char *file_name)
{
   if (file_name == NULL) {
      return NULL;
   }

   static char absolute_file_path[PATH_MAX];
   memset(absolute_file_path, 0, PATH_MAX * sizeof(char));

   if (realpath(file_name, absolute_file_path) == NULL) {
      return NULL;
   }
   return absolute_file_path;
}

int daemon_init_process()
{
   pid_t process_id = 0;
   pid_t sid = 0;

   process_id = fork();
   if (process_id < 0)  {
      PRINT_ERR("Fork: could not initialize daemon process!")
      return -1;
   } else if (process_id > 0) {
      NULLP_TEST_AND_FREE(logs_path)
      free_output_file_strings_and_streams();
      fprintf(stdout, "[INFO] PID of daemon process: %d.", process_id);
      exit(EXIT_SUCCESS);
   }

   umask(0);
   sid = setsid();
   if (sid < 0) {
      PRINT_ERR("Setsid: calling process is process group leader!")
      return -1;
   }

   return 0;
}

void free_output_file_strings_and_streams()
{
   if (supervisor_debug_log_fd != NULL) {
      fclose(supervisor_debug_log_fd);
      supervisor_debug_log_fd = NULL;
   }
   if (supervisor_log_fd != NULL) {
      fclose(supervisor_log_fd);
      supervisor_log_fd = NULL;
   }
   if (statistics_fd != NULL) {
      fclose(statistics_fd);
      statistics_fd = NULL;
   }
   if (inst_event_fd != NULL) {
      fclose(inst_event_fd);
      inst_event_fd = NULL;
   }
}

int load_configuration()
{
   int rc;

   VERBOSE(N_INFO,"Loading sysrepo configuration");

   rc = vector_init(&insts_v, 10);
   if (rc != 0) {
      VERBOSE(N_ERR, "Failed to allocate memory for module instances")
      return -1;
   }

   rc = vector_init(&mods_v, 10);
   if (rc != 0) {
      VERBOSE(N_ERR, "Failed to allocate memory for modules")
      return -1;
   }

   rc = vector_init(&mgrps_v, 5);
   if (rc != 0) {
      VERBOSE(N_ERR, "Failed to allocate memory for module groups vector")
      return -1;
   }

   pthread_mutex_lock(&config_lock);
   rc = ns_config_load(sr_conn_link.sess);
   pthread_mutex_unlock(&config_lock);
   if (rc != 0) {
      VERBOSE(N_ERR, "Failed to load config from sysrepo tree")
      return rc;
   }

   module_group_t *tmp2 = NULL;
   VERBOSE(DEBUG, "Printing module groups:")
   FOR_EACH_IN_VEC(mgrps_v, tmp2) {
      print_module_group(tmp2);
   }

   module_t *tmp3 = NULL;
   VERBOSE(DEBUG, "Printing modules:")
   FOR_EACH_IN_VEC(mods_v, tmp3) {
      print_module(tmp3);
   }


   instance_t *tmp;
   VERBOSE(DEBUG, "Printing instances:")
   FOR_EACH_IN_VEC(insts_v, tmp) {
      print_instance(tmp);
   }

   VERBOSE(N_INFO, "Loaded %d module groups", mgrps_v.total)
   VERBOSE(N_INFO, "Loaded %d modules", insts_v.total)
   VERBOSE(N_INFO, "Loaded %d instances", insts_v.total)

   return 0;
}

void sig_handler(int catched_signal)
{
   switch (catched_signal) {
      case SIGPIPE:
         break;

      case SIGTERM:
         VERBOSE(N_WARN,"SIGTERM catched -> I'm going to terminate myself!")
         supervisor_stopped = true;
         terminate_insts_at_exit = true;
         supervisor_exit_code = EXIT_SUCCESS;
         break;

      case SIGINT:
         VERBOSE(N_WARN,"SIGINT catched -> I'm going to terminate myself!")
         supervisor_stopped = true;
         terminate_insts_at_exit = true; // TODO change to false later
         supervisor_exit_code = EXIT_SUCCESS;
         break;

      case SIGQUIT:
         VERBOSE(N_WARN,"SIGQUIT catched -> I'm going to terminate myself!")
         supervisor_stopped = true;
         terminate_insts_at_exit = false;
         supervisor_exit_code = EXIT_SUCCESS;
         break;

      case SIGSEGV:
         VERBOSE(N_WARN,"Ouch, SIGSEGV catched -> I'm going to terminate myself!")
         terminate_supervisor(false);
         exit(EXIT_FAILURE);
   }
   // Falls back to normal execution
}

void supervisor_routine()
{
   uint32_t running_insts_cnt = 0;
   uint64_t period_cnt = 0;

   VERBOSE(DEBUG, "Starting supervisor routine")
   while (supervisor_stopped == false) {
      VERBOSE(DEBUG, "-----routine loop-----")

      /* Lock instances list so that async changes from sysrepo don't
       * interfere with this routine */
      pthread_mutex_lock(&config_lock);
      {
         // Start instances that should be running
         insts_start();
         running_insts_cnt = get_running_insts_cnt();
         VERBOSE(DEBUG, "Found %d running instances", running_insts_cnt)

         // Check which instances need to be killed and kill them
         insts_stop_sigint();
         insts_stop_sigkill();
         running_insts_cnt = get_running_insts_cnt();
         VERBOSE(DEBUG, "Found %d running instances", running_insts_cnt)

         // Update CPU and memory usage
         insts_update_resources_usage();

         // Handle connection between supervisor and instances via service interface
         check_insts_connections();
         get_service_ifces_stats();
         running_insts_cnt = get_running_insts_cnt();
      }
      pthread_mutex_unlock(&config_lock);
      usleep(SERVICE_THREAD_SLEEP_IN_MICSEC);

      // -------------
      // DEBUG OPTIONS START
      if (((period_cnt % 30) == 0) && (running_insts_cnt > 0)) {
         print_statistics();
      }
      period_cnt++;

      // test turning off
      //modules->enabled = false;
      //if ((period_cnt % 2) == 0) {
      //   VERBOSE(DEBUG,"w")
      //}

      if ((period_cnt % 2) == 0) {
         supervisor_stopped = true;
      }
      // -------------
      // DEBUG OPTIONS STOP
      // -------------
   }
   VERBOSE(DEBUG, "Supervisor routine finished")

   { // Disconnect from running instances
      VERBOSE(DEBUG, "Disconnecting from running instances")
      pthread_mutex_lock(&config_lock);
      instance_t *inst = NULL;
      FOR_EACH_IN_VEC(insts_v, inst) {
         disconnect_from_inst(inst);
      }
      pthread_mutex_unlock(&config_lock);
   }

   terminate_supervisor(terminate_insts_at_exit);
   exit(supervisor_exit_code);
}

static inline void print_statistics()
{
   time_t t = 0;
   time(&t);
   char *stats_buffer = make_formated_statistics((uint8_t) 1);

   if (stats_buffer == NULL) {
      return;
   }
   VERBOSE(STATISTICS, "------> %s", ctime(&t));
   VERBOSE(STATISTICS, "%s", stats_buffer);

   NULLP_TEST_AND_FREE(stats_buffer);
}

static char *make_formated_statistics(uint8_t stats_mask)
{
   unsigned int size_of_buffer = 5 * DEFAULT_SIZE_OF_BUFFER;
   char *buffer = (char *) calloc(size_of_buffer, sizeof(char));
/*
   uint8_t print_ifc_stats = FALSE, print_cpu_stats = FALSE, print_memory_stats = FALSE;
   unsigned int x, y;
   int ptr = 0;

   // Decide which stats should be printed according to the stats mask
   if ((stats_mask & (uint8_t) 1) == (uint8_t) 1) {
      print_ifc_stats = TRUE;
   }
   if ((stats_mask & (uint8_t) 2) == (uint8_t) 2) {
      print_cpu_stats = TRUE;
   }
   if ((stats_mask & (uint8_t) 4) == (uint8_t) 4) {
      print_memory_stats = TRUE;
   }

   if (print_ifc_stats == TRUE) {
      for (x = 0; x < loaded_modules_cnt; x++) {
         if (modules[x].module_status == TRUE && modules[x].module_service_ifc_isconnected == TRUE) {
            if (modules[x].in_ifces_data != NULL) {
               for (y = 0; y < modules[x].total_in_ifces_cnt; y++) {
                  ptr += sprintf(buffer + ptr, "%s,in,%c,%s,%"PRIu64",%"PRIu64"\n", modules[x].mod_name,
                                 modules[x].in_ifces_data[y].type,
                                 (modules[x].in_ifces_data[y].id != NULL ? modules[x].in_ifces_data[y].id : "none"),
                                 modules[x].in_ifces_data[y].recv_msg_cnt,
                                 modules[x].in_ifces_data[y].recv_buff_cnt);
                  if (strlen(buffer) >= (3 * size_of_buffer) / 5) {
                     size_of_buffer += size_of_buffer / 2;
                     buffer = (char *) realloc (buffer, size_of_buffer * sizeof(char));
                     memset(buffer + ptr, 0, size_of_buffer - ptr);
                  }
               }
            }
            if (modules[x].out_ifces_data != NULL) {
               for (y = 0; y < modules[x].total_out_ifces_cnt; y++) {
                  ptr += sprintf(buffer + ptr, "%s,out,%c,%s,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"\n", modules[x].mod_name,
                                 modules[x].out_ifces_data[y].type,
                                 (modules[x].out_ifces_data[y].id != NULL ? modules[x].out_ifces_data[y].id : "none"),
                                 modules[x].out_ifces_data[y].sent_msg_cnt,
                                 modules[x].out_ifces_data[y].dropped_msg_cnt,
                                 modules[x].out_ifces_data[y].sent_buff_cnt,
                                 modules[x].out_ifces_data[y].autoflush_cnt);
                  if (strlen(buffer) >= (3 * size_of_buffer) / 5) {
                     size_of_buffer += size_of_buffer / 2;
                     buffer = (char *) realloc (buffer, size_of_buffer * sizeof(char));
                     memset(buffer + ptr, 0, size_of_buffer - ptr);
                  }
               }
            }
         }
      }
   }

   if (print_cpu_stats == TRUE) {
      for (x=0; x<loaded_modules_cnt; x++) {
         if (modules[x].module_status == TRUE) {
            ptr += sprintf(buffer + ptr, "%s,cpu,%lu,%lu\n", modules[x].mod_name,
                           modules[x].last_period_percent_cpu_usage_kernel_mode,
                           modules[x].last_period_percent_cpu_usage_user_mode);
            if (strlen(buffer) >= (3*size_of_buffer)/5) {
               size_of_buffer += size_of_buffer/2;
               buffer = (char *) realloc (buffer, size_of_buffer * sizeof(char));
               memset(buffer + ptr, 0, size_of_buffer - ptr);
            }
         }
      }
   }

   if (print_memory_stats == TRUE) {
      for (x=0; x<loaded_modules_cnt; x++) {
         if (modules[x].module_status == TRUE) {
            ptr += sprintf(buffer + ptr, "%s,mem,%lu\n", modules[x].mod_name,
                           modules[x].mem_vms / (1024*1024)); // to convert B to MB, divide by 1024*1024
            if (strlen(buffer) >= (3*size_of_buffer)/5) {
               size_of_buffer += size_of_buffer/2;
               buffer = (char *) realloc (buffer, size_of_buffer * sizeof(char));
               memset(buffer + ptr, 0, size_of_buffer - ptr);
            }
         }
      }
   }*/

   return buffer;
}

static inline void get_service_ifces_stats()
{
   instance_t *inst = NULL;

   FOR_EACH_IN_VEC(insts_v, inst) {
      if (inst->service_ifc_connected == false) {
         continue;
      }

      VERBOSE(V3, "Sending request for stats of instance '%s'", inst->name)
      if (send_ifc_stats_request(inst) == -1) {
            VERBOSE(N_ERR," Error while sending request to instance '%s'", inst->name);
         disconnect_from_inst(inst);
      }
   }

   // Loops are separated so that there is some time for clients to respond
   FOR_EACH_IN_VEC(insts_v, inst) {
      instance_set_running_status(inst);
      if (inst->running == false) {
         continue;
      }
      recv_ifc_stats(inst);
   }
}

inline static int recv_ifc_stats(instance_t *inst)
{
   char *buffer = NULL;
   int rc;
   uint32_t buffer_size = 0;
   service_msg_header_t resp_header;
   resp_header.com = SERVICE_GET_COM;
   resp_header.data_size = 0;

   // Check whether the inst is running and is connected with supervisor via service interface
   if (false == inst->running || false == inst->service_ifc_connected) {
      // TODO irelevant? checkuje se to uz predtim
      return -1;
   }

   VERBOSE(V3, "Receiving reply from inst (%s)", instance_tree_path(inst))
   // Receive reply header
   rc = service_recv_data(inst, sizeof(service_msg_header_t), (void *) &resp_header);
   if (rc == -1) {
      VERBOSE(N_ERR, "Error while receiving reply header from inst (%s).",
              instance_tree_path(inst))
      goto error_label;
   }

   // Check if the reply is OK
   if (resp_header.com != SERVICE_OK_REPLY) {
      VERBOSE(N_ERR, "Wrong reply from inst (%s).", instance_tree_path(inst))
      goto error_label;
   }

   if (resp_header.data_size > buffer_size) {
      // Reallocate buffer for incoming data
      buffer_size += (resp_header.data_size - buffer_size) + 1;
      buffer = (char *) calloc(1, buffer_size * sizeof(char));
   }

   VERBOSE(V3, "Receiving stats from inst (%s)", instance_tree_path(inst))
   // Receive inst stats in json format
   if (service_recv_data(inst, resp_header.data_size, (void *) buffer) == -1) {
      VERBOSE(N_ERR, "Error while receiving stats from inst %s.", inst->name)
      goto error_label;
   }

   // Decode json and save stats into inst structure
   VERBOSE(DEBUG, "%s", buffer)
   if (service_decode_inst_stats(buffer, inst) == -1) {
      VERBOSE(N_ERR, "Error while receiving stats from inst %s.", inst->name);
      goto error_label;
   }

   NULLP_TEST_AND_FREE(buffer)

   return 0;

error_label:
   disconnect_from_inst(inst);
   NULLP_TEST_AND_FREE(buffer)
   return -1;
}

static int service_decode_inst_stats(char *data, instance_t *inst)
{
   size_t arr_idx = 0;

   json_error_t error;
   json_t *json_struct = NULL;
   json_t *ifces_arr = NULL;
   json_t *in_ifc_cnts  = NULL;
   json_t *out_ifc_cnts = NULL;
   json_t *value = NULL;

   uint32_t ifc_cnt = 0;
   const char *str = NULL;

   interface_t *ifc;
   ifc_in_stats_t *in_stats;
   ifc_out_stats_t *out_stats;

#define FETCH_VALUE_OR_ERR(json_object, key) do { \
   value = json_object_get((json_object), (key)) ;\
   if ((key) == NULL) { \
      VERBOSE(N_WARN, "Could not get JSON key '%s' on line %d", (key), __LINE__) \
      goto err_cleanup; \
   } \
} while (0);

#define IS_JSON_OBJ_OR_ERR(json_object) do { \
   if (json_is_object((json_object)) == 0) { \
      VERBOSE(N_ERR, "Loaded value is not JSON object on line %d", __LINE__) \
      goto err_cleanup; \
   } \
} while (0);

#define IS_JSON_ARR_OR_ERR(json_object) do { \
   if (json_is_array((json_object)) == 0) { \
      VERBOSE(N_ERR, "Loaded value is not JSON array on line %d", __LINE__) \
      goto err_cleanup; \
   } \
} while (0);

#define FETCH_IFC_ID_OR_ERR(json_object, stats) do { \
   FETCH_VALUE_OR_ERR((json_object), "ifc_id") \
   str = json_string_value(value); \
   if (str == NULL) { \
      VERBOSE(N_ERR, "Could not get JSON string value of 'id_ifc' on line %d", \
              __LINE__) \
      goto err_cleanup; \
   } \
   if ((stats)->id == NULL) { \
      (stats)->id = strdup(str); \
      if ((stats)->id == NULL) { \
         NO_MEM_ERR \
         goto err_cleanup; \
      } \
   } else if (strcmp((stats)->id, str) != 0) { \
      NULLP_TEST_AND_FREE((stats)->id) \
      (stats)->id = strdup(str); \
      if ((stats)->id == NULL) { \
         NO_MEM_ERR \
         goto err_cleanup; \
      } \
   } \
} while (0); \

   // Parse received instances counters in json format
   json_struct = json_loads(data , 0, &error);
   if (json_struct == NULL) {
      VERBOSE(N_ERR, "Could not convert instances stats to json structure on "
              "line %d: %s", error.line, error.text);
      return -1;
   }
   IS_JSON_OBJ_OR_ERR(json_struct)

   // Check number of input interfaces the inst is running with
   FETCH_VALUE_OR_ERR(json_struct, "in_cnt")
   ifc_cnt = (uint32_t) json_integer_value(value);

   if (ifc_cnt != inst->in_ifces.total) {
      /* This could mean that Supervisor has different configuration
       *  than the inst is running with. */
      VERBOSE(N_ERR, "Instance has different number of IN interfaces (%d)"
            " than configuration from supervisor (%d).",
              ifc_cnt, inst->in_ifces.total)
      goto err_cleanup;
   }

   // Check number of output interfaces the inst is running with
   FETCH_VALUE_OR_ERR(json_struct, "out_cnt")
   ifc_cnt = (uint32_t) json_integer_value(value);

   if (ifc_cnt != inst->out_ifces.total) {
      /* This could mean that Supervisor has different configuration
       *  than the inst is running with. */
      VERBOSE(N_ERR, "Instance has different number of OUT interfaces (%d)"
            " than configuration from supervisor (%d).",
              ifc_cnt, inst->out_ifces.total)
      goto err_cleanup;
   }

   if (inst->in_ifces.total > 0) {
      /* Get value from the key "in" from json root elem (it should be an array
       *  of json objects - every object contains counters of one input interface) */
      FETCH_VALUE_OR_ERR(json_struct, "in")
      ifces_arr = value;
      IS_JSON_ARR_OR_ERR(ifces_arr)

      json_array_foreach(ifces_arr, arr_idx, in_ifc_cnts) {
         ifc = inst->in_ifces.items[arr_idx];
         if (ifc == NULL || (arr_idx + 1) > inst->in_ifces.total) {
            VERBOSE(N_ERR, "Instance stats specify more IN interfaces than supervisor"
                    " configured")
            goto err_cleanup;
         }
         IS_JSON_OBJ_OR_ERR(in_ifc_cnts)
         in_stats = ifc->stats;
         FETCH_VALUE_OR_ERR(in_ifc_cnts, "messages")
         in_stats->recv_msg_cnt = (uint64_t) json_integer_value(value);
         FETCH_VALUE_OR_ERR(in_ifc_cnts, "buffers")
         in_stats->recv_buff_cnt = (uint64_t) json_integer_value(value);
         FETCH_VALUE_OR_ERR(in_ifc_cnts, "ifc_type")
         in_stats->type = (char) json_integer_value(value);
         FETCH_VALUE_OR_ERR(in_ifc_cnts, "ifc_state")
         in_stats->state = (uint8_t) json_integer_value(value);
         FETCH_IFC_ID_OR_ERR(in_ifc_cnts, in_stats)
      }
   }


   if (inst->out_ifces.total > 0) {
      /* Get value from the key "out" from json root elem (it should be an array
       *  of json objects - every object contains counters of one output interface) */
      FETCH_VALUE_OR_ERR(json_struct, "out")
      ifces_arr = value;
      IS_JSON_ARR_OR_ERR(ifces_arr)

      json_array_foreach(ifces_arr, arr_idx, out_ifc_cnts) {
         // TODO does it really comes in correct order, yea? yeash
         ifc = inst->out_ifces.items[arr_idx];
         if (ifc == NULL || (arr_idx + 1) > inst->out_ifces.total) {
            VERBOSE(N_ERR, "Instance stats specify more OUT interfaces than supervisor"
                    " configured")
            goto err_cleanup;
         }
         IS_JSON_OBJ_OR_ERR(out_ifc_cnts)
         out_stats = ifc->stats;
         FETCH_VALUE_OR_ERR(out_ifc_cnts, "sent-messages")
         out_stats->sent_msg_cnt = (uint64_t) json_integer_value(value);
         FETCH_VALUE_OR_ERR(out_ifc_cnts, "dropped-messages")
         out_stats->dropped_msg_cnt = (uint64_t) json_integer_value(value);
         FETCH_VALUE_OR_ERR(out_ifc_cnts, "buffers")
         out_stats->sent_buff_cnt = (uint64_t) json_integer_value(value);
         FETCH_VALUE_OR_ERR(out_ifc_cnts, "autoflushes")
         out_stats->autoflush_cnt = (uint64_t) json_integer_value(value);
         FETCH_VALUE_OR_ERR(out_ifc_cnts, "num_clients")
         out_stats->num_clients = (uint32_t) json_integer_value(value);
         FETCH_VALUE_OR_ERR(out_ifc_cnts, "type")
         out_stats->type = (char) json_integer_value(value);
         FETCH_IFC_ID_OR_ERR(out_ifc_cnts, out_stats)
      }
   }

   json_decref(json_struct);
   return 0;

err_cleanup:
   if (json_struct != NULL) {
      json_decref(json_struct);
   }
   return -1;
}

static int service_recv_data(instance_t *inst, uint32_t size, void *data)
{
   int num_of_timeouts = 0;
   int total_received = 0;
   int last_received = 0;

   while (total_received < size) {
      last_received = (int) recv(inst->service_sd, data + total_received,
                           size - total_received, MSG_DONTWAIT);
      if (last_received == 0) {
         VERBOSE(DEBUG, "Instance service thread closed its socket, im done!")
         return -1;
      } else if (last_received == -1) {
         if (errno == EAGAIN || errno == EWOULDBLOCK) {
            num_of_timeouts++;
            if (num_of_timeouts >= 3) {
               VERBOSE(N_ERR, "Timeout while receiving from inst %s!", inst->name)
               return -1;
            } else {
               usleep(SERVICE_WAIT_BEFORE_TIMEOUT);
               continue;
            }
         }
         VERBOSE(N_ERR, "Error while receiving from inst %s! (errno=%d)",
                 inst->name, errno)
         return -1;
      }
      total_received += last_received;
   }
   return 0;
}

static int send_ifc_stats_request(const instance_t *inst)
{
   service_msg_header_t req_header;
   req_header.com = SERVICE_GET_COM;
   req_header.data_size = 0;

   ssize_t sent = 0;
   uint32_t req_size = sizeof(req_header);
   uint32_t num_of_timeouts = 0;
   uint32_t total_sent = 0;

   while (total_sent < req_size) {
      sent = send(inst->service_sd, (void *) (&req_header + total_sent),
                  req_size - total_sent, MSG_DONTWAIT);
      if (-1 == sent) {
         if (errno == EAGAIN || errno == EWOULDBLOCK) {
            num_of_timeouts++;
            if (num_of_timeouts >= 3) {
               VERBOSE(N_ERR, "Timeouted while sending to inst '%s'!", inst->name);
               return -1;
            } else {
               usleep(SERVICE_WAIT_BEFORE_TIMEOUT);
               continue;
            }
         }
         VERBOSE(N_ERR, "Error while sending to inst %s !", inst->name);
         return -1;
      }
      total_sent += sent;
   }
   return 0;

}

static inline void disconnect_from_inst(instance_t *inst)
{
   if (inst->service_ifc_connected) {
      VERBOSE(V3,"Disconnecting from inst '%s'", inst->name);
      // Close if not closed already
      if (inst->service_sd != -1) {
         close(inst->service_sd);
         inst->service_sd = -1;
      }
      inst->service_ifc_connected = false;
   }
   inst->service_ifc_conn_timer = 0;
}

static inline void check_insts_connections()
{
   instance_t * inst = NULL;
   bool instance_not_connected;
   bool has_ifc_stats;

   FOR_EACH_IN_VEC(insts_v, inst) {
      instance_not_connected = (inst->service_ifc_connected == false);
      has_ifc_stats = ((inst->in_ifces.total + inst->out_ifces.total) > 0);
      // Check connection between instance and supervisor
      if (inst->running && instance_not_connected && has_ifc_stats) {
         // Connect to instances only once per NUM_SERVICE_IFC_PERIOD
         inst->service_ifc_conn_timer++;
         if ((inst->service_ifc_conn_timer % NUM_SERVICE_IFC_PERIOD) == 1) {
            // Check inst socket descriptor, closed socket has descriptor set to -1
            if (inst->service_sd != -1) {
               close(inst->service_sd);
               inst->service_sd = -1;
            }
            VERBOSE(DEBUG, "Trying to connect to inst %s", inst->name)
            connect_to_inst(inst);
         }
      }
   }
}

static void connect_to_inst(instance_t *inst)
{
   /* sock_name size is length of "service_PID" where PID is
    *  max 5 chars (8 + 5 + 1 zero terminating) */
   char sock_name[14];
   int sockfd;
   tcpip_socket_addr_t addr;

   memset(sock_name, 0, 14 * sizeof(char));
   sprintf(sock_name, "service_%d", inst->pid);

   memset(&addr, 0, sizeof(addr));

   addr.unix_sa.sun_family = AF_UNIX;
   snprintf(addr.unix_sa.sun_path,
            sizeof(addr.unix_sa.sun_path) - 1,
            trap_default_socket_path_format,
            sock_name);

   VERBOSE(V3, "Instance (%s) has socket %s", instance_tree_path(inst),
           addr.unix_sa.sun_path)

   sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (sockfd == -1) {
      VERBOSE(N_ERR,"Error while opening socket for connection with inst %s.",
              inst->name)
      inst->service_ifc_connected = false;
      return;
   }
   if (connect(sockfd, (struct sockaddr *) &addr.unix_sa, sizeof(addr.unix_sa)) == -1) {
      VERBOSE(N_ERR,
              "Error while connecting to inst %s with socket '%s'",
              inst->name,
              addr.unix_sa.sun_path)

      inst->service_ifc_connected = false;
      close(sockfd);
      return;
   }
   inst->service_sd = sockfd;
   inst->service_ifc_connected = true;
   inst->service_ifc_conn_timer = 0; // Successfully connected, reset connection timer
   VERBOSE(V3,"Connected to inst '%s'.", inst->name);
}

static void insts_update_resources_usage()
{
   instance_t *inst = NULL;

   FOR_EACH_IN_VEC(insts_v, inst) {
      if (inst->running) {
         inst_get_stats(inst);
         inst_get_vmrss(inst);
      }
   }
}
static inline void inst_get_vmrss(instance_t *inst)
{
   const char delim[2] = " ";
   char *line = NULL,
         *token = NULL,
         *endptr = NULL;
   size_t line_len = 0;
   FILE *fd = NULL;
   char path[DEFAULT_SIZE_OF_BUFFER];
   uint64_t resident_set_size = 0;

   memset(path, 0, DEFAULT_SIZE_OF_BUFFER * sizeof(char));
   snprintf(path, DEFAULT_SIZE_OF_BUFFER * sizeof(char), "/proc/%d/status", inst->pid);

   fd = fopen(path, "r");
   if (fd == NULL) {
      VERBOSE(DEBUG, "Failed to open stats file of %s (PID=%d)",
              instance_tree_path(inst), inst->pid)
      goto cleanup;
   }

   while (-1 != getline(&line, &line_len, fd)) {
      if (strstr(line, "VmRSS") != NULL) {
         token = strtok(line, delim);
         while (token != NULL) {
            resident_set_size = strtoul(token, &endptr, 10);
            if (resident_set_size > 0) {
               inst->mem_rss = resident_set_size;
               break;
            }
            token = strtok(NULL, delim);
         }
         break;
      }
   }
cleanup:
   if (fd != NULL) {
      fclose(fd);
   }
   NULLP_TEST_AND_FREE(line)
}

static inline void inst_get_stats(instance_t *inst)
{

   const char delim[2] = " ";
   char *line = NULL,
         *token = NULL,
         *endptr = NULL;
   FILE *proc_stat_fd = NULL;
   char path[DEFAULT_SIZE_OF_BUFFER];
   uint64_t tmp_num = 0,
         diff_total_cpu = 0,
         new_total_cpu = 0;



   if (get_total_cpu_usage(&new_total_cpu) == -1) {
      return;
   }

   diff_total_cpu = new_total_cpu - last_total_cpu;
   if (0 == diff_total_cpu) {
      return;
   }
   last_total_cpu = new_total_cpu;

   memset(path, 0, DEFAULT_SIZE_OF_BUFFER * sizeof(char));
   snprintf(path, DEFAULT_SIZE_OF_BUFFER * sizeof(char), "/proc/%d/stat", inst->pid);
   proc_stat_fd = fopen(path, "r");

   if (proc_stat_fd == NULL) {
      VERBOSE(V1, "Failed to open stats file of inst with PID=%d", inst->pid)
      goto cleanup;
   }

   if (getline(&line, &tmp_num, proc_stat_fd) == -1) {
      VERBOSE(V1, "Failed to read stats of inst with PID=%d", inst->pid)
      goto cleanup;
   }

   token = strtok(line, delim);
   if (token == NULL) {
      goto cleanup;
   }

   for (int position = 1; token != NULL && position <= 23; position++) {
      switch (position) {
         case 14:
            // position of "user mode time" field in /proc/pid/stat file is 14
            tmp_num = strtoul(token, &endptr, 10);
            if (endptr == token) {
               VERBOSE(V1, "Unable to get user mode time for inst PID=%d", inst->pid)
               continue;
            }
            inst->last_cpu_perc_umode = (uint64_t)
                  (100 * ((double)(tmp_num - inst->last_cpu_umode) / (double)diff_total_cpu));
            inst->last_cpu_umode = tmp_num;
            break;

         case 15:
            // position of "kernel mode time" field in /proc/pid/stat file is 15
            tmp_num = strtoul(token, &endptr, 10);
            if (endptr == token) {
               VERBOSE(V1, "Unable to get kernel mode time for inst PID=%d", inst->pid)
               continue;
            }
            inst->last_cpu_perc_kmode = (uint64_t)
                  (100 * ((double)(tmp_num - inst->last_cpu_kmode) / (double)diff_total_cpu));
            inst->last_cpu_kmode = tmp_num;
            break;

         case 23:
            // position of "virtual memory size" field in /proc/pid/stat file is 23
            tmp_num = strtoul(token, &endptr, 10);
            if (endptr == token) {
               continue;
            }
            inst->mem_vms = tmp_num;
            break;

         default:break;
      }

      token = strtok(NULL, delim);
   }

cleanup:
   if (proc_stat_fd != NULL) {
      fclose(proc_stat_fd);
   }
   NULLP_TEST_AND_FREE(line)
}

static int get_total_cpu_usage(uint64_t *total_cpu_usage)
{
   uint64_t num = 0;
   const char delim[2] = " ";
   char *line = NULL,
         *token = NULL,
         *endptr = NULL;
   int ret_val = 0;
   size_t line_len = 0;
   FILE *proc_stat_fd = fopen("/proc/stat","r");

   if (proc_stat_fd == NULL) {
      return -1;
   }
   if (getline(&line, &line_len, proc_stat_fd) == -1) {
      ret_val = -1;
      goto cleanup;
   }

   token = strtok(line, delim);
   if (token == NULL) {
      ret_val = -1;
      goto cleanup;
   }
   if (strcmp(token, "cpu") != 0) {
      ret_val = -1;
      goto cleanup;
   }
   token = strtok(NULL, delim);

   while (token != NULL) {
      num = strtoul(token, &endptr, 10);
      if (endptr == token) {
         // if there were no character convertable to numeric in token
         ret_val = -1;
         break;
      }
      *total_cpu_usage += num;
      token = strtok(NULL, delim);
   }

cleanup:
   fclose(proc_stat_fd);
   NULLP_TEST_AND_FREE(line);
   return ret_val;
}

void insts_start()
{
   instance_t *inst = NULL;
   time_t time_now;
   VERBOSE(V3, "Updating instances status")

   FOR_EACH_IN_VEC(insts_v, inst) {
      if (inst->module->group->enabled == false || inst->enabled == false ||
            inst->running == true) {
         continue;
      }

      time(&time_now);

      // Has it been less than minute since last start attempt?
      if (time_now - inst->restart_time <= 60) {
         inst->restarts_cnt++;
         if (inst->restarts_cnt == inst->max_restarts_minute) {
            VERBOSE(MOD_EVNT,
                    "Instance '%s' reached restart limit. Disabling.",
                    inst->name)
            inst->enabled = false;
         } else {
            inst_start(inst);
         }
      } else {
         inst->restarts_cnt = 0;
         inst->restart_time = time_now;
         inst_start(inst);
      }
   }
}

static void inst_start(instance_t *inst)
{
   char log_path_out[PATH_MAX];
   char log_path_err[PATH_MAX];

   VERBOSE(V2, "Starting '%s'", instance_tree_path(inst))

   memset(log_path_err, 0, PATH_MAX);
   memset(log_path_out, 0, PATH_MAX);
   sprintf(log_path_out,"%s%s/%s_stdout", logs_path, INSTANCES_LOGS_DIR_NAME, inst->name);
   sprintf(log_path_err,"%s%s/%s_stderr", logs_path, INSTANCES_LOGS_DIR_NAME, inst->name);

   time_t time_now;
   time(&time_now);

   fflush(stdout);
   inst->pid = fork();

   if (inst->pid == -1) {
      inst->running = false;
      VERBOSE(N_ERR,"Fork: could not fork supervisor process!")
      return;
   }

   if (inst->pid != 0) {
      // Running as parent
      inst->is_my_child = true;
      inst->running = true;
   } else {
      // Running as forked child
      int fd_stdout = open(log_path_out, O_RDWR | O_CREAT | O_APPEND, PERM_LOGSDIR);
      int fd_stderr = open(log_path_err, O_RDWR | O_CREAT | O_APPEND, PERM_LOGSDIR);

      if (fd_stdout != -1) {
         dup2(fd_stdout, 1); //stdout
         close(fd_stdout);
      }
      if (fd_stderr != -1) {
         dup2(fd_stderr, 2); //stderr
         close(fd_stderr);
      }

      /*
       * TODO rewrite. why? just comment?
       * Important for sending SIGINT to supervisor.
       * Modules can't receive the signal too !!!
       * */
      setsid();

      // Don't even think about rewriting this to VERBOSE macro
      fprintf(stdout, "[INFO]%s Supervisor executed command: ",
              get_formatted_time());
      for(int i = 0; inst->exec_args[i] != NULL; i++) {
         fprintf(stdout, " %s", inst->exec_args[i]);
      }
      fprintf(stdout, "\n");

      // Make sure execution message gets written to logs files
      fflush(stdout);
      fflush(stderr);

      execv(inst->module->path, inst->exec_args);

      { // If correctly started, this won't be executed
         VERBOSE(N_ERR,
                 "Could not execute '%s' binary! (execvp errno=%d)",
                 inst->name,
                 errno)
         fflush(stdout);
         //terminate_supervisor(false);
         VERBOSE(DEBUG, " trying to exit")
         close(fd_stdout);
         close(fd_stderr);
         _exit(EXIT_FAILURE);
         VERBOSE(DEBUG, " trying to exit")
      }
   }
}

static void insts_save_running_pids()
{
   VERBOSE(V3, "Saving PIDs of all modules")
   /* Module group + inst max by YANG 2x255 + static part of 47 chars,
    * that leaves quite enough space for ns_root_sr_path */
   instance_t *inst = NULL;
   static char xpath[4096];
   int rc;
   sr_val_t *val;

   // Make sure we are in STARTUP datastore
   rc = sr_session_switch_ds(sr_conn_link.sess, SR_DS_STARTUP);
   if (rc != SR_ERR_OK) {
      VERBOSE(N_ERR, "Failed to switch SR session for saving PIDs. (err: %s)",
              sr_strerror(rc))
      return;
   }

   rc = sr_new_val(NULL, &val);
   if (rc != SR_ERR_OK) {
      VERBOSE(N_ERR, "Failed to create new value for saving PIDs "
            "of running modules")
      return;
   }
   val->type = SR_UINT32_T;
   val->xpath = xpath;

   FOR_EACH_IN_VEC(insts_v, inst) {
      if (inst->running == false) {
         continue;
      }

      memset(xpath, 0, 4096);
      sprintf(xpath, "%s/module-group[name='%s']/module[name='%s']"
                    "/instance[name='%s']/last-pid",
              ns_root_sr_path,
              inst->module->group->name,
              inst->module->name,
              inst->name);

      val->data.uint32_val = (uint32_t) inst->pid;
      rc = sr_set_item(sr_conn_link.sess, xpath, val,
                       SR_EDIT_DEFAULT | SR_EDIT_NON_RECURSIVE);
      if (rc != SR_ERR_OK) {
         VERBOSE(N_ERR, "Failed to save PID for %s (err: %s)",
                 instance_tree_path(inst),
                 sr_strerror(rc))
      }
   }
   rc = sr_commit(sr_conn_link.sess);
   if (rc != SR_ERR_OK) {
      VERBOSE(N_ERR, "Failed to commit PID saving.")
   }

   sr_free_val(val);
}

 /* --END local fns-- */
 /****************************************************************************/