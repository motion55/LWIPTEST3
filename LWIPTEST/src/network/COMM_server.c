/*
 * COMM_server.c
 *
 * Created: 7/23/2014 3:49:56 PM
 *  Author: Butch
 */ 

#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"

#include "COMM_server.h"

#ifndef _debug_
#define _debug_	0
#endif

#define RX_MAX_SIZE    		256
#define TX_MAX_SIZE    		1024
#define COMM_SERVER_PORT	10001

#ifndef	CloseConnection
#define	CloseConnection()
#endif

//////////////////////////////////////////////////////////

static Bool bIsConnected = false;

static U8 TX_Buffer[TX_MAX_SIZE];
static U16 TX_Beg = 0, TX_End = 0;

static U8 RX_Buffer[RX_MAX_SIZE];
static U16 rx_nHead, rx_nTail;

static struct tcp_pcb *COMM_server_pcb;
static struct tcp_pcb *last_tpcb = NULL;

/* ECHO protocol states */
enum COMM_server_states
{
  ES_NONE = 0,
  ES_ACCEPTED,
  ES_RECEIVED,
  ES_CLOSING
};

/* structure for maintaing connection infos to be passed as argument 
   to LwIP callbacks*/
struct COMM_server_struct
{
  u8_t state;             /* current connection state */
  struct tcp_pcb *pcb;    /* pointer on the current tcp_pcb */
  struct pbuf *p;         /* pointer on the received/to be transmitted pbuf */
};


static err_t COMM_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t COMM_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void COMM_server_error(void *arg, err_t err);
static err_t COMM_server_poll(void *arg, struct tcp_pcb *tpcb);
static err_t COMM_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void COMM_server_send(struct tcp_pcb *tpcb, struct COMM_server_struct *es);
static void COMM_server_connection_close(struct tcp_pcb *tpcb, struct COMM_server_struct *es);

/*////////////////////////////////////////////////////////////////////////*/
static void COMM_recv(const void *data, u16_t len);
static void TX_Data(struct tcp_pcb *tpcb, struct COMM_server_struct *es);
static void ProcessData(struct tcp_pcb *tpcb, struct COMM_server_struct *es);
static void queue_rcv_buf(unsigned char ch);
/*////////////////////////////////////////////////////////////////////////*/


/**
  * @brief  Initializes the tcp echo server
  * @param  None
  * @retval None
  */
void COMM_server_start(void)
{
  /* create new tcp pcb */
  COMM_server_pcb = tcp_new();

  if (COMM_server_pcb != NULL)
  {
    err_t err;
    
    /* bind echo_pcb to port 7 (ECHO protocol) */
    err = tcp_bind(COMM_server_pcb, IP_ADDR_ANY, COMM_SERVER_PORT);
    
    if (err == ERR_OK)
    {
      /* start tcp listening for echo_pcb */
      COMM_server_pcb = tcp_listen(COMM_server_pcb);
      
      /* initialize LwIP tcp_accept callback function */
      tcp_accept(COMM_server_pcb, COMM_server_accept);
    }
    else 
    {
#if _debug_
      debug_send("Can not bind pcb\n");
#endif	  
    }
  }
  else
  {
#if _debug_
    debug_send("Can not create new pcb\n");
#endif	
  }
}

/**
  * @brief  This function is the implementation of tcp_accept LwIP callback
  * @param  arg: not used
  * @param  newpcb: pointer on tcp_pcb struct for the newly created tcp connection
  * @param  err: not used 
  * @retval err_t: error status
  */
static err_t COMM_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  err_t ret_err;
  struct COMM_server_struct* es;

  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(err);

  /* set priority for the newly accepted tcp connection newpcb */
  tcp_setprio(newpcb, TCP_PRIO_MIN);

  /* allocate structure es to maintain tcp connection informations */
  es = (struct COMM_server_struct *)mem_malloc(sizeof(struct COMM_server_struct));
  if (es != NULL)
  {
    es->state = ES_ACCEPTED;
    es->pcb = newpcb;
    es->p = NULL;
    
    /* pass newly allocated es structure as argument to newpcb */
    tcp_arg(newpcb, es);
    
    /* initialize lwip tcp_recv callback function for newpcb  */ 
    tcp_recv(newpcb, COMM_server_recv);
    
    /* initialize lwip tcp_err callback function for newpcb  */
    tcp_err(newpcb, COMM_server_error);
    
    /* initialize lwip tcp_poll callback function for newpcb */
    tcp_poll(newpcb, COMM_server_poll, 1);
    
    TX_Beg = 0;
    TX_End = 0;
	bIsConnected = true;

#if _debug_
    debug_send("\r\n Connection Accepted.");
#endif

#if 0
    TX_Buffer[0] = 0;
    TX_Buffer[1] = 0;
    TX_Buffer[2] = 0;
    TX_Buffer[3] = 0;

    es->p = pbuf_alloc(PBUF_TRANSPORT, 4 , PBUF_POOL);

    if (es->p)
    {
      /* copy data to pbuf */
      pbuf_take(es->p, (char*)TX_Buffer, 4);

      /* send data */
      COMM_server_send(newpcb,es);
    }
#endif
    ret_err = ERR_OK;
  }
  else
  {
    /* return memory error */
    ret_err = ERR_MEM;
  }
  return ret_err;  
}


/**
  * @brief  This function is the implementation for tcp_recv LwIP callback
  * @param  arg: pointer on a argument for the tcp_pcb connection
  * @param  tpcb: pointer on the tcp_pcb connection
  * @param  pbuf: pointer on the received pbuf
  * @param  err: error information regarding the reveived pbuf
  * @retval err_t: error code
  */
static err_t COMM_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  struct COMM_server_struct * es;
  err_t ret_err;

  LWIP_ASSERT("arg != NULL",arg != NULL);
  
  es = (struct COMM_server_struct *)arg;
  
  /* if we receive an empty tcp frame from client => close connection */
  if (p == NULL)
  {
    /* remote host closed connection */
    es->state = ES_CLOSING;
    if(es->p == NULL)
    {
       /* we're done sending, close connection */
       COMM_server_connection_close(tpcb, es);
    }
    else
    {
      /* we're not done yet */
      /* acknowledge received packet */
      tcp_sent(tpcb, COMM_server_sent);
      
      /* send remaining data*/
      COMM_server_send(tpcb, es);
    }
    ret_err = ERR_OK;
  }   
  /* else : a non empty frame was received from client but for some reason err != ERR_OK */
  else if(err != ERR_OK)
  {
    /* free received pbuf*/
    if (p != NULL)
    {
      es->p = NULL;
      pbuf_free(p);
    }
    ret_err = err;
  }
  else if(es->state == ES_ACCEPTED)
  {
    /* first data chunk in p->payload */
    es->state = ES_RECEIVED;
    
    /* initialize LwIP tcp_sent callback function */
    tcp_sent(tpcb, COMM_server_sent);
    
#ifdef	_JEIL60_H_
    while (p != NULL) {
    	struct pbuf *ptr = p;

    	COMM_recv(ptr->payload, ptr->len);
    	ProcessData(tpcb, es);

        p = ptr->next;
        if (p!=NULL)
        {
        	pbuf_ref(p);
        }

        pbuf_free(ptr);
        tcp_recved(tpcb, ptr->len);
    }
#else
    /* store reference to incoming pbuf (chain) */
    es->p = p;
    
    /* send back the received data (echo) */
    COMM_server_send(tpcb, es);
#endif
    ret_err = ERR_OK;
  }
  else if (es->state == ES_RECEIVED)
  {
#if 1
    while (p != NULL) {
    	struct pbuf *ptr = p;

    	COMM_recv(ptr->payload, ptr->len);
    	ProcessData(tpcb, es);

        p = ptr->next;
        if (p!=NULL)
        {
        	pbuf_ref(p);
        }

        tcp_recved(tpcb, ptr->len);
        pbuf_free(ptr);
    }
#else
    /* more data received from client and previous data has been already sent*/
    if(es->p == NULL)
    {
      es->p = p;
  
      /* send back received data */
      COMM_server_send(tpcb, es);
    }
    else
    {
      struct pbuf *ptr;

      /* chain pbufs to the end of what we recv'ed previously  */
      ptr = es->p;
      pbuf_chain(ptr,p);
    }
#endif
    ret_err = ERR_OK;
  }
  
  /* data received when connection already closed */
  else
  {
    /* Acknowledge data reception */
    tcp_recved(tpcb, p->tot_len);
    
    /* free pbuf and do nothing */
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  return ret_err;
}

/**
  * @brief  This function implements the tcp_err callback function (called
  *         when a fatal tcp_connection error occurs. 
  * @param  arg: pointer on argument parameter 
  * @param  err: not used
  * @retval None
  */
static void COMM_server_error(void *arg, err_t err)
{
  struct COMM_server_struct *es;

  LWIP_UNUSED_ARG(err);

  es = (struct COMM_server_struct *)arg;
  if (es != NULL)
  {
    /*  free es structure */
    mem_free(es);
  }
}

/**
  * @brief  This function implements the tcp_poll LwIP callback function
  * @param  arg: pointer on argument passed to callback
  * @param  tpcb: pointer on the tcp_pcb for the current tcp connection
  * @retval err_t: error code
  */
static err_t COMM_server_poll(void *arg, struct tcp_pcb *tpcb)
{
  err_t ret_err;
  struct COMM_server_struct *es;

  es = (struct COMM_server_struct *)arg;
  if (es != NULL)
  {
    if (es->p != NULL)
    {
      /* there is a remaining pbuf (chain) , try to send data */
      COMM_server_send(tpcb, es);
    }
    else
    {
      /* no remaining pbuf (chain)  */
      if(es->state == ES_CLOSING)
      {
        /*  close tcp connection */
        COMM_server_connection_close(tpcb, es);
      }
      else
      {
   		if (TX_End>TX_Beg)
   			TX_Data(tpcb, es);
      }
    }
    ret_err = ERR_OK;
  }
  else
  {
    /* nothing to be done */
    tcp_abort(tpcb);
    ret_err = ERR_ABRT;
  }
  return ret_err;
}

/**
  * @brief  This function implements the tcp_sent LwIP callback (called when ACK
  *         is received from remote host for sent data) 
  * @param  None
  * @retval None
  */
static err_t COMM_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  struct COMM_server_struct *es;

  LWIP_UNUSED_ARG(len);

  es = (struct COMM_server_struct *)arg;
  
  if(es->p != NULL)
  {
    /* still got pbufs to send */
    COMM_server_send(tpcb, es);
  }
  else
  {
    /* if no more data to send and client closed connection*/
    if(es->state == ES_CLOSING)
      COMM_server_connection_close(tpcb, es);
    else
    {
   	  if (TX_End>TX_Beg)
   	  {
   		TX_Data(tpcb, es);
   	  }
    }
  }
  return ERR_OK;
}


/**
  * @brief  This function is used to send data for tcp connection
  * @param  tpcb: pointer on the tcp_pcb connection
  * @param  es: pointer on echo_state structure
  * @retval None
  */
static void COMM_server_send(struct tcp_pcb *tpcb, struct COMM_server_struct *es)
{
  struct pbuf *ptr;
  err_t wr_err = ERR_OK;
 
  while ((wr_err == ERR_OK) &&
         (es->p != NULL) && 
         (es->p->len <= tcp_sndbuf(tpcb)))
  {
    
    /* get pointer on pbuf from es structure */
    ptr = es->p;

    /* enqueue data for transmission */
    wr_err = tcp_write(tpcb, ptr->payload, ptr->len, TCP_WRITE_FLAG_COPY);

    if (wr_err == ERR_OK)
    {
      /* continue with next pbuf in chain (if any) */
      es->p = ptr->next;
      
      if(es->p != NULL)
      {
        /* increment reference count for es->p */
        pbuf_ref(es->p);
      }
      
      /* free pbuf: will free pbufs up to es->p (because es->p has a reference count > 0) */
      pbuf_free(ptr);
     
   }
   else if(wr_err == ERR_MEM)
   {
      /* we are low on memory, try later / harder, defer to poll */
     es->p = ptr;
   }
   else
   {
     /* other problem ?? */
   }
  }
}

/**
  * @brief  This functions closes the tcp connection
  * @param  tcp_pcb: pointer on the tcp connection
  * @param  es: pointer on echo_state structure
  * @retval None
  */
static void COMM_server_connection_close(struct tcp_pcb *tpcb, struct COMM_server_struct *es)
{
  
	/* remove all callbacks */
	tcp_arg(tpcb, NULL);
	tcp_sent(tpcb, NULL);
	tcp_recv(tpcb, NULL);
	tcp_err(tpcb, NULL);
	tcp_poll(tpcb, NULL, 0);
  
	/* delete es structure */
	if (es != NULL)
	{
		mem_free(es);
	}  
  
	/* close tcp connection */
	tcp_close(tpcb);

	CloseConnection();

	bIsConnected = false;

	#if _debug_
	debug_send("\r\n Session Closed.");
	#endif
}

inline void queue_rcv_buf(unsigned char ch)
{
	RX_Buffer[rx_nHead] = ch;
	rx_nHead = (rx_nHead+1)&(RX_MAX_SIZE-1);
	if (rx_nHead==rx_nTail)
	{
		rx_nTail = (rx_nTail+1)&(RX_MAX_SIZE-1);
	}
}

static void COMM_recv(const void *data, u16_t len)
{
	char ch;
	char *message = (char*)data;
	while (len>0) {
		ch = *message;
		queue_rcv_buf(ch);
		message++;
		len--;
	}
}

static void TX_Data(struct tcp_pcb *tpcb, struct COMM_server_struct *es)
{
    /* allocate pbuf */
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, TX_End, PBUF_POOL);
    /* copy data to pbuf */
    pbuf_take(p, (char*)TX_Buffer, TX_End);
    TX_Beg = 0;
    TX_End = 0;

    if (es->p==NULL)
    {
    	es->p = p;
    	COMM_server_send(tpcb, es);
    }
    else
    {
    	struct pbuf *ptr = es->p;
        pbuf_chain(ptr, p);
    }
}

void COMM_server_putdata(unsigned char ch)
{
	if (TX_End<TX_MAX_SIZE)
	{
		TX_Buffer[TX_End] = ch;
		TX_End++;
	}
}

static void ProcessData(struct tcp_pcb *tpcb, struct COMM_server_struct *es)
{
	last_tpcb = tpcb;

//	ReceiveChars(echoserver_senddata, NULL);

	if (TX_End>TX_Beg)
		TX_Data(tpcb, es);
}

#ifdef	_JEIL60_H_
int	COMM_server_getdata(unsigned char *ch)
{
	EthernetTask(time_of_day);
	
	if	(rx_nHead!=rx_nTail)
	{
		*ch = RX_Buffer[rx_nTail++];
		if (rx_nTail>=RX_MAX_SIZE) rx_nTail = 0;

		return 1;
	}
	else return 0;
}
#endif

inline Bool COMM_IsConnected(void)
{
	return bIsConnected;
}
