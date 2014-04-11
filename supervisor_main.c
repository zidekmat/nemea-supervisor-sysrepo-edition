/**
 * \file supervisor_main.c
 * \brief Supervisor main function.
 * \author Marek Svepes <svepemar@fit.cvut.cz>
 * \date 2014
 */
/*
 * Copyright (C) 2013 CESNET
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

#include "supervisor_api.h"
#include <stdio.h>

int main (int argc, char * argv [])
{
	if(api_initialization(&argc, argv)){
		return 0;
	}

	int ret_val = 0;
	
	while ((ret_val = api_print_menu()) != 10) {
		switch(ret_val) {
		case 0:
			api_set_verbose_level();
			break;

		case 1:
			api_start_configuration();
			break;
			
		case 2:
			api_stop_configuration();
			break;
				
		case 3:
			api_start_module();		
			break;

		case 4:
			api_stop_module();
			break;

		case 5:
			api_set_module_enabled();
			break;

		case 6:
			api_show_running_modules_status();
			break;

		case 7:
			api_show_available_modules();
			break;

		case 8:
			api_show_graph();
			break;

		case 9:
			api_run_temp_conf();
			break;

		default:
			printf("Wrong input.\n");
			break;
		}
	}

	api_quit();

	return 0;
}