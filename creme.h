#ifndef CREME_H
#define CREME_H

#include <sys/types.h>

/*
   Version de la librairie creme.
   L'idee est qu'elle puisse evoluer independamment de biceps.
*/
#define CREME_VERSION "1.0"

/*
   Fonctions exportees par la librairie.
   Ce sont elles que biceps utilisera.
*/

/* gestion du serveur BEUIP dans un processus fils */
int creme_beuip_start(const char *pseudo);
int creme_beuip_stop(void);
pid_t creme_get_server_pid(void);

/* commandes client envoyees au serveur local 127.0.0.1:9998 */
int creme_mess_liste(void);
int creme_mess_msg(const char *pseudo, const char *message);
int creme_mess_all(const char *message);

#endif
