/*
 * Wireless Networks Group, Department of Networks Engineering, UPC, Barcelonatech (2015)
 */

/**
 * \file
 *      An implementation of the CoAP Congestion Control Simple /Advanced  (draft-bormann-core-cocoa-02).
 *      This implementation only considers advanced congestion control mechanisms for the exchange of
 *      CON messages.
 *
 * \author
 *     August Betzler <august.betzler@entel.upc.edu>
 */

#include "er-coap-transactions.h"


#define DEBUG   DEBUG_NONE
#include "net/uip-debug.h"

#define SMALLVBF 1.5
#define LARGEVBF 2.5
#define LOWERVBFTHRESHOLD 1
#define UPPERVBFTHRESHOLD 3

#define MAXIMUM_OVERALLRTO 60

#define ALPHA 0.125
#define BETA 0.25
#define STRONGK 4
#define WEAKK   1

#define STRONGESTIMATOR 0
#define WEAKESTIMATOR 1
#define OVERALLESTIMATOR 2

// K-Value and weighting for the strong and weak estimators, respectively
static uint8_t kValue[] = {4,1};
static double weight[] = {0.5, 0.25};

MEMB(rtt_estimations_memb, coap_rtt_estimations_t, COAP_MAX_RTT_ESTIMATIONS);
LIST(rtt_estimations_list);

//AUGUST:
uint8_t
countTransactionsForAddress(uip_ipaddr_t *addr, list_t transactions_list){
	coap_transaction_t *t = NULL;
	uint8_t counter = 0;
	  for (t = (coap_transaction_t*)list_head(transactions_list); t; t = t->next){
	    if (uip_ipaddr_cmp(addr, &t->addr)){
	     counter++;
	    }
	  }
	  return counter;
}

//AUGUST:
void
coap_delete_rtt_by_freshness() {
  coap_rtt_estimations_t *t = NULL;
  clock_time_t newest_rtt = 0xffff;
  for (t = (coap_rtt_estimations_t*)list_head(rtt_estimations_list); t; t = t->next){
    if (t->lastupdated[0] < newest_rtt){
      newest_rtt = t->lastupdated[0];
      PRINTF("Older entry found (%lu)\n", (unsigned long) newest_rtt);
    }
  }

  for (t = (coap_rtt_estimations_t*)list_head(rtt_estimations_list); t; t = t->next){
    if (t->lastupdated[0] == newest_rtt){
    	 list_remove(rtt_estimations_list, t);
    	 memb_free(&rtt_estimations_memb, t);
    	 PRINTF("RTT entry deleted\n");
    }
  }

  return;
}

//AUGUST
clock_time_t coap_check_rto_state(clock_time_t rto, clock_time_t oldrto){
	PRINTF("Rto in:%lu|%lu\n", (unsigned long)rto, (unsigned long)oldrto);
	if(oldrto < (LOWERVBFTHRESHOLD * CLOCK_SECOND)){
		// A low initial RTO, use a large BF
		return  (rto * LARGEVBF);
	}else if(oldrto > (UPPERVBFTHRESHOLD * CLOCK_SECOND)){
		// A high initial RTO, use a small BF
		return (rto * SMALLVBF);
	}else{
		// A normal initial RTO, use the default BF
		return rto << 1;
	}
}

//AUGUST
coap_rtt_estimations_t *
coap_new_rtt_estimation(clock_time_t rtt, uip_ipaddr_t *addr, uint8_t retransmissions)
{
  coap_rtt_estimations_t *e = memb_alloc(&rtt_estimations_memb);
  if (e){
	  uint8_t i;
	for(i=0; i<=2; i++){
		// Initialize the RTO estimators (0 = strong, 1 = weak, 2 = overall)
		e->rto[i]= COAP_INITIAL_RTO;
		e->lastupdated[0] = clock_time();
	}
	e->rttsmissed  = 0;
	uip_ipaddr_copy(&e->addr, addr);
    list_add(rtt_estimations_list, e); /* List itself makes sure same element is not added twice. */
  }else{
   PRINTF("RTT storage full. Delete old entry.\n");
   coap_delete_rtt_by_freshness();
   return coap_new_rtt_estimation(rtt, addr, retransmissions);
  }

  return e;
}

//AUGUST
void
coap_update_rtt_estimation(uip_ipaddr_t* transactionAddr, clock_time_t rtt, uint8_t retransmissions){
	coap_rtt_estimations_t *t = NULL;
	PRINTF("Measured RTT:%lu\n", (unsigned long) rtt);
		  for (t = (coap_rtt_estimations_t*)list_head(rtt_estimations_list); t; t = t->next){
		    if (uip_ipaddr_cmp(transactionAddr, &t->addr)){
		    	PRINTF("Found existing RTO entry, updating it\n");
		    	// found an entry with an already used address, now perform calculations to update the RTO
		    	t->lastupdated[OVERALLESTIMATOR] = clock_time();
		    	uint8_t rttType;
		    	if(retransmissions == 0){
		    		rttType = STRONGESTIMATOR;
		    		t->rttsmissed = 0;
		    	}else{
		    		rttType = WEAKESTIMATOR;
		    		t->rttsmissed += retransmissions;
					if(t->rttsmissed > 3){
						// After more than three retransmissions without getting a valid RTT measurement,
						// the strong_rtt is reset. This feature is not detailed in the draft!
						t->rtt[STRONGESTIMATOR] = COAP_INITIAL_RTO;
						t->rto[STRONGESTIMATOR] = COAP_INITIAL_RTO;
						t->rttsmissed = 0;
					}
		    	}

				t->lastupdated[rttType] = clock_time();
				if(t->rto[rttType] == COAP_INITIAL_RTO){
					// This is the first RTT measurement we get, so set the strong_rtt to be the measured RTT
					t->rtt[rttType] = rtt;
					t->rttvar[rttType] = rtt * 0.5; // set rttvar to be 0.5 * rtt;
				}else{
					// Another strong RTT measurement was made, update the vars
					t->rttvar[rttType] = (1-BETA) * t->rttvar[rttType] + BETA * abs(t->rtt[rttType] - rtt);
					t->rtt[rttType] = (1-ALPHA) * t->rtt[rttType] + ALPHA * rtt;
				}

				t->rto[rttType] =  t->rtt[rttType] + (t->rttvar[rttType] * kValue[rttType]);
				t->rto[rttType] = (t->rto[rttType] > MAXRTO_VALUE) ? MAXRTO_VALUE : t->rto[rttType];

				t->rto[OVERALLESTIMATOR] = (1 - weight[rttType]) * t->rto[rttType] + weight[rttType] * t->rto[OVERALLESTIMATOR];
				PRINTF("SRTT:%lu|RTTVAR:%lu|RTO:%lu|overallRTO:%lu\n", (unsigned long) t->rtt[rttType], (unsigned long) t->rttvar[rttType], (unsigned long) t->rto[rttType], (unsigned long) t->rto[OVERALLESTIMATOR]);

			}
		  }
	    return;
}

//AUGUST
clock_time_t
coap_check_rtt_estimation(uip_ipaddr_t* transactionAddr, list_t transactions_list){
	coap_rtt_estimations_t *t = NULL;
	uint8_t destCount = 0;
	// Go through the list of RTT info stored for different destination endpoints
	  for (t = (coap_rtt_estimations_t*)list_head(rtt_estimations_list); t; t = t->next){
	    if (uip_ipaddr_cmp(transactionAddr, &t->addr)){
	    	// Found an existing transaction to the same destination
	    	if(t->rto[OVERALLESTIMATOR] == COAP_INITIAL_RTO){
	    		// We still do not have a valid RTT measurement, so for each transaction
	    		// already directed to the same destination node, increase the multiplier (Blind RTO)
	    		  coap_transaction_t *trans = NULL;
		    	  for (trans = (coap_transaction_t*)list_head(transactions_list); trans; trans = trans->next){
		    	    if (uip_ipaddr_cmp(transactionAddr, &trans->addr)){
		    	    	PRINTF("Initial RTT entry for transaction found\n");
			    		destCount++;
		    	    }
		    	  }

		    	  if(destCount > 1){
		    		  // FIXME: The previous loop will always count one more than necessary,
		    		  // so decrease if entries were found
		    		  destCount--;
		    	  }
	    	}else{


	    		// TODO: It could be evaluated if the strong/weak estimators also need updates
	    		// For now, this feature is disabled.
//				uint8_t i;
//				for(i=0;i<=1;i++){
//					clock_time_t difference = clock_time() - t->lastupdated[i];
//					if(t->rto[i] < CLOCK_SECOND && difference > (16* t->rto[i])){
//					// Too much time past since last updated and RTO is very small: Update RTT and RTTVAR of strong/weak estimators
//						t->rtt[i] = CLOCK_SECOND;
//						t->rttvar[i] = CLOCK_SECOND >> 1;
//						t->lastupdated[i] = clock_time();
//					}
//				}

				// A simple implementation of the aging mechanism for the overall estimator
				clock_time_t difference = clock_time() - t->lastupdated[OVERALLESTIMATOR];
				if(t->rto[OVERALLESTIMATOR] < CLOCK_SECOND && difference > (16* t->rto[OVERALLESTIMATOR])){
					t->rto[OVERALLESTIMATOR] = CLOCK_SECOND;
				}
				if(t->rto[OVERALLESTIMATOR] > (3 * CLOCK_SECOND) && difference > (4* t->rto[OVERALLESTIMATOR])){
									t->rto[OVERALLESTIMATOR] = CLOCK_SECOND + (0.5 * t->rto[OVERALLESTIMATOR]);
								}
	    		// A maximum of 60 s is allowed for the RTO estimator, if above, truncate it
	    		if(t->rto[OVERALLESTIMATOR] > (MAXIMUM_OVERALLRTO * CLOCK_SECOND))
	    			t->rto[OVERALLESTIMATOR] = (MAXIMUM_OVERALLRTO * CLOCK_SECOND);

	    		return t->rto[OVERALLESTIMATOR];
	    	}
	    }
	  }

	  if(destCount == 0){
		  PRINTF("Need new RTT entry\n");
		  // We do not have an rtt_estimation entry for this destination -> Insert one
		  coap_new_rtt_estimation(0, transactionAddr, 0);
	  }
	  clock_time_t initialrto = ((COAP_INITIAL_RTO + (random_rand() % (clock_time_t) COAP_RESPONSE_TIMEOUT_BACKOFF_MASK)) << destCount);
	  PRINTF("blindRTO: %lu (with already %u ongoing transactions)\n", initialrto, destCount);
	  return  initialrto > (32 * CLOCK_SECOND) ? (32 * CLOCK_SECOND) : initialrto ;
}
