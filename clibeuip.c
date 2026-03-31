/*****
* Client BEUIP pour les tests
* Envoie des commandes au serveur local sur 127.0.0.1:9998
*****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9998
#define LBUF 512

/*
   Construit un message simple de forme :
   code + "BEUIP" + donnees
*/
void construitMessageSimple(char *dest, char code, char *donnees)
{
    dest[0] = code;
    strcpy(dest + 1, "BEUIP");

    if (donnees != NULL) {
        strcpy(dest + 6, donnees);
    } else {
        dest[6] = '\0';
    }
}

/*
   Construit un message de code '4' :
   code + "BEUIP" + pseudo + '\0' + message
*/
int construitMessagePrive(char *dest, char *pseudo, char *message)
{
    dest[0] = '4';
    strcpy(dest + 1, "BEUIP");
    strcpy(dest + 6, pseudo);
    strcpy(dest + 6 + strlen(pseudo) + 1, message);

    return 6 + strlen(pseudo) + 1 + strlen(message);
}

int main(int argc, char *argv[])
{
    int sid;
    struct sockaddr_in Serv;
    char buf[LBUF + 1];
    int taille;

    if (argc < 2) {
        fprintf(stderr, "Utilisation :\n");
        fprintf(stderr, "  %s liste\n", argv[0]);
        fprintf(stderr, "  %s msg pseudo message\n", argv[0]);
        fprintf(stderr, "  %s all message\n", argv[0]);
        fprintf(stderr, "  %s quit\n", argv[0]);
        return 1;
    }

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return 2;
    }

    bzero(&Serv, sizeof(Serv));
    Serv.sin_family = AF_INET;
    Serv.sin_port = htons(PORT);
    Serv.sin_addr.s_addr = inet_addr("127.0.0.1");

    /*
       Commande liste : code '3'
    */
    if (strcmp(argv[1], "liste") == 0) {
        construitMessageSimple(buf, '3', NULL);
        taille = 6;
    }

    /*
       Commande message prive : code '4'
    */
    else if (strcmp(argv[1], "msg") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Utilisation : %s msg pseudo message\n", argv[0]);
            close(sid);
            return 3;
        }

        taille = construitMessagePrive(buf, argv[2], argv[3]);
    }

    /*
       Commande message global : code '5'
    */
    else if (strcmp(argv[1], "all") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Utilisation : %s all message\n", argv[0]);
            close(sid);
            return 4;
        }

        construitMessageSimple(buf, '5', argv[2]);
        taille = 6 + strlen(argv[2]);
    }

    /*
       Commande quit : code '0'
       Ici elle sert juste a simuler une sortie locale.
       Dans le vrai sujet, c'est plutot le serveur qui l'envoie en broadcast a la fin.
    */
    else if (strcmp(argv[1], "quit") == 0) {
        construitMessageSimple(buf, '0', "local");
        taille = 6 + strlen("local");
    }

    else {
        fprintf(stderr, "Commande inconnue\n");
        close(sid);
        return 5;
    }

    if (sendto(sid, buf, taille, 0,
               (struct sockaddr *)&Serv, sizeof(Serv)) == -1) {
        perror("sendto");
        close(sid);
        return 6;
    }

    printf("Commande envoyee au serveur local\n");

    close(sid);
    return 0;
}
