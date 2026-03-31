/*****
* Serveur BEUIP
* derive du serveur UDP avec accuse de reception
*
* Format du message d'identification :
* octet 1  : code '1' broadcast ou '2' accuse de reception
* octets 2-6 : chaine "BEUIP"
* octets 7-fin : pseudo de l'expediteur
*****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT 9998
#define LBUF 512
#define MAXCOUPLES 255
#define BROADCAST_IP "192.168.88.255"

/*
   Structure qui represente un couple (adresse IP + pseudo)
*/
typedef struct {
    unsigned long ip;
    char pseudo[256];
} Couple;

/* buffer de reception */
char buf[LBUF + 1];

/* adresse locale du serveur */
struct sockaddr_in SockConf;

/* table des presents sur le reseau */
static Couple Table[MAXCOUPLES];
static int NCouples = 0;

/*
   Cette fonction transforme une adresse IP en chaine lisible.
*/
char *addrip(unsigned long A)
{
    static char b[16];

    sprintf(b, "%u.%u.%u.%u",
            (unsigned int)(A >> 24 & 0xFF),
            (unsigned int)(A >> 16 & 0xFF),
            (unsigned int)(A >> 8 & 0xFF),
            (unsigned int)(A & 0xFF));

    return b;
}

/*
   Cette fonction verifie si un couple (ip + pseudo) est deja dans la table.
   Cela evite les doublons.
*/
int dejaPresent(unsigned long ip, char *pseudo)
{
    int i;

    for (i = 0; i < NCouples; i++) {
        if (Table[i].ip == ip && strcmp(Table[i].pseudo, pseudo) == 0) {
            return 1;
        }
    }

    return 0;
}

/*
   Cette fonction ajoute un couple (ip + pseudo) dans la table
   si celui-ci n'est pas deja present.
*/
void ajouteCouple(unsigned long ip, char *pseudo)
{
    if (pseudo == NULL || *pseudo == '\0') {
        return;
    }

    if (dejaPresent(ip, pseudo)) {
#ifdef TRACE
        printf("[TRACE] Couple deja connu : %s - %s\n", addrip(ip), pseudo);
#endif
        return;
    }

    if (NCouples >= MAXCOUPLES) {
        fprintf(stderr, "Table des couples pleine\n");
        return;
    }

    Table[NCouples].ip = ip;
    strncpy(Table[NCouples].pseudo, pseudo, sizeof(Table[NCouples].pseudo) - 1);
    Table[NCouples].pseudo[sizeof(Table[NCouples].pseudo) - 1] = '\0';
    NCouples++;

#ifdef TRACE
    printf("[TRACE] Ajout couple : %s - %s\n", addrip(ip), pseudo);
#endif
}

/*
   Cette fonction construit un message BEUIP.
   code = '1' pour broadcast, '2' pour accuse de reception
*/
void construitMessage(char *dest, char code, char *pseudo)
{
    dest[0] = code;
    strcpy(dest + 1, "BEUIP");

    if (pseudo != NULL) {
        strcpy(dest + 6, pseudo);
    } else {
        dest[6] = '\0';
    }
}

/*
   Cette fonction verifie que le message recu respecte l'entete BEUIP.
   Elle verifie :
   - le code ('1' ou '2')
   - la chaine "BEUIP"
*/
int messageValide(char *msg, int n)
{
    if (n < 6) {
        return 0;
    }

    if (msg[0] != '1' && msg[0] != '2') {
        return 0;
    }

    if (strncmp(msg + 1, "BEUIP", 5) != 0) {
        return 0;
    }

    return 1;
}

/*
   Cette fonction affiche la table des couples connus.
   C'est pratique pour verifier le fonctionnement.
*/
void afficheTable(void)
{
    int i;

    printf("---- Table des presents ----\n");
    for (i = 0; i < NCouples; i++) {
        printf("%2d : %s - %s\n", i + 1, addrip(Table[i].ip), Table[i].pseudo);
    }
    printf("----------------------------\n");
}

int main(int N, char *P[])
{
    int sid, n;
    int opt = 1;
    socklen_t ls;
    char msg[LBUF + 1];
    char ar[LBUF + 1];
    char *pseudo;
    struct sockaddr_in Sock;
    struct sockaddr_in SockBroad;

    /*
       Le serveur doit recevoir exactement un parametre :
       le pseudo choisi par l'utilisateur.
    */
    if (N != 2) {
        fprintf(stderr, "Utilisation : %s pseudo\n", P[0]);
        return 1;
    }

    pseudo = P[1];

    /*
       Creation du socket UDP.
    */
    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return 2;
    }

    /*
       Pour avoir le droit d'envoyer en broadcast,
       on active l'option SO_BROADCAST sur le socket.
    */
    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_BROADCAST");
        return 3;
    }

    /*
       Initialisation de l'adresse locale pour le bind.
    */
    bzero(&SockConf, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) {
        perror("bind");
        return 4;
    }

    printf("Serveur BEUIP actif sur le port %d avec le pseudo %s\n", PORT, pseudo);

    /*
       Preparation de l'adresse de broadcast 192.168.88.255:9998
    */
    bzero(&SockBroad, sizeof(SockBroad));
    SockBroad.sin_family = AF_INET;
    SockBroad.sin_port = htons(PORT);
    SockBroad.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    /*
       Des que le bind est fait, on envoie un message broadcast
       de code '1' avec notre pseudo.
    */
    construitMessage(msg, '1', pseudo);

    if (sendto(sid, msg, strlen(msg + 6) + 6, MSG_CONFIRM,
               (struct sockaddr *)&SockBroad, sizeof(SockBroad)) == -1) {
        perror("sendto broadcast");
    } else {
        printf("Broadcast d'identification envoye\n");
    }

    /*
       Le serveur attend ensuite les messages BEUIP.
    */
    for (;;) {
        ls = sizeof(Sock);

        n = recvfrom(sid, (void *)buf, LBUF, 0,
                     (struct sockaddr *)&Sock, &ls);

        if (n == -1) {
            perror("recvfrom");
            continue;
        }

        buf[n] = '\0';

#ifdef TRACE
        printf("[TRACE] Datagramme recu depuis %s\n",
               addrip(ntohl(Sock.sin_addr.s_addr)));
#endif

        /*
           Verification de l'entete :
           - code
           - chaine BEUIP
        */
        if (!messageValide(buf, n)) {
#ifdef TRACE
            printf("[TRACE] Message ignore : entete invalide\n");
#endif
            continue;
        }

        /*
           Le pseudo commence a l'octet 7, donc a l'indice 6.
        */
        ajouteCouple(ntohl(Sock.sin_addr.s_addr), buf + 6);

#ifdef TRACE
        printf("[TRACE] Code recu : %c\n", buf[0]);
        printf("[TRACE] Pseudo recu : %s\n", buf + 6);
        afficheTable();
#endif

        /*
           Si on recoit un message de code '1',
           on renvoie un accuse de reception de code '2'
           contenant notre pseudo.
        */
        if (buf[0] == '1') {
            construitMessage(ar, '2', pseudo);

            if (sendto(sid, ar, strlen(ar + 6) + 6, MSG_CONFIRM,
                       (struct sockaddr *)&Sock, ls) == -1) {
                perror("sendto AR");
            } else {
#ifdef TRACE
                printf("[TRACE] AR envoye a %s\n",
                       addrip(ntohl(Sock.sin_addr.s_addr)));
#endif
            }
        }
    }

    return 0;
}
