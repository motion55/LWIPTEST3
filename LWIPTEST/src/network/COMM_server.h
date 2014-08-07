/*
 * COMM_server.h
 *
 * Created: 7/23/2014 3:50:23 PM
 *  Author: Butch
 */ 

#ifndef _COMM_SERVER_H_
#define _COMM_SERVER_H_

void COMM_server_start(void);

void COMM_server_putdata(unsigned char ch);

int	COMM_server_getdata(unsigned char *ch);

Bool COMM_IsConnected(void);

#endif /* _COMM_SERVER_H_ */