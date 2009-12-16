/*
Copyright (c) 2009  Eucalyptus Systems, Inc.	

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by 
the Free Software Foundation, only version 3 of the License.  
 
This file is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.  

You should have received a copy of the GNU General Public License along
with this program.  If not, see <http://www.gnu.org/licenses/>.
 
Please contact Eucalyptus Systems, Inc., 130 Castilian
Dr., Goleta, CA 93101 USA or visit <http://www.eucalyptus.com/licenses/> 
if you need additional information or have any questions.

This file may incorporate work covered under the following copyright and
permission notice:

  Software License Agreement (BSD License)

  Copyright (c) 2008, Regents of the University of California
  

  Redistribution and use of this software in source and binary forms, with
  or without modification, are permitted provided that the following
  conditions are met:

    Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

    Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. USERS OF
  THIS SOFTWARE ACKNOWLEDGE THE POSSIBLE PRESENCE OF OTHER OPEN SOURCE
  LICENSED MATERIAL, COPYRIGHTED MATERIAL OR PATENTED MATERIAL IN THIS
  SOFTWARE, AND IF ANY SUCH MATERIAL IS DISCOVERED THE PARTY DISCOVERING
  IT MAY INFORM DR. RICH WOLSKI AT THE UNIVERSITY OF CALIFORNIA, SANTA
  BARBARA WHO WILL THEN ASCERTAIN THE MOST APPROPRIATE REMEDY, WHICH IN
  THE REGENTS’ DISCRETION MAY INCLUDE, WITHOUT LIMITATION, REPLACEMENT
  OF THE CODE SO IDENTIFIED, LICENSING OF THE CODE SO IDENTIFIED, OR
  WITHDRAWAL OF THE CODE CAPABILITY TO THE EXTENT NEEDED TO COMPLY WITH
  ANY SUCH LICENSES OR RIGHTS.
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "axis2_skel_EucalyptusCC.h"

#include <server-marshal.h>
#include <handlers.h>
#include <storage.h>
#include <vnetwork.h>
#include <euca_auth.h>
#include <misc.h>

#include "data.h"
#include "client-marshal.h"

#define SUPERUSER "eucalyptus"

// local globals
int init=0;
sem_t *initLock=NULL;

// to be stored in shared memory
ccConfig *config=NULL;
sem_t *configLock=NULL;

ccInstanceCache *instanceCache=NULL;
sem_t *instanceCacheLock=NULL;

vnetConfig *vnetconfig=NULL;
sem_t *vnetConfigLock=NULL;

int doAttachVolume(ncMetadata *ccMeta, char *volumeId, char *instanceId, char *remoteDev, char *localDev) {
  int i, j, rc, start, stop, ret=0;
  ccInstance *myInstance;
  ncStub *ncs;
  time_t op_start, op_timer;
  
  i = j = 0;
  myInstance = NULL;
  op_start = time(NULL);
  op_timer = OP_TIMEOUT;
  
  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG,"AttachVolume(): called\n");
  if (!volumeId || !instanceId || !remoteDev || !localDev) {
    logprintfl(EUCAERROR, "bad input params to AttachVolume(): volumeId=%s, instanceId=%s, remoteDev=%s, localDev=%s\n", volumeId ? volumeId : "UNSET", instanceId ? instanceId : "UNSET", remoteDev ? remoteDev : "UNSET", localDev ? localDev : "UNSET");
    return(1);
  }

  rc = find_instanceCacheId(instanceId, &myInstance);
  if (!rc) {
    // found the instance in the cache
    if (myInstance) {
      start = myInstance->ncHostIdx;
      stop = start+1;
      free(myInstance);
    }
  } else {
    start = 0;
    stop = config->numResources;
  }
  
  sem_wait(configLock);
  for (j=start; j<stop; j++) {
    // read the instance ids
    logprintfl(EUCAINFO,"AttachVolume(): calling attach volume (%s) on (%s)\n", instanceId, config->resourcePool[j].hostname);
    if (1) {
      int pid, status;
      pid = fork();
      if (pid == 0) {
	ret = 0;
	ncs = ncStubCreate(config->resourcePool[j].ncURL, NULL, NULL);
	if (config->use_wssec) {
	  rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	}
	logprintfl(EUCADEBUG, "calling attachVol on NC: %s\n",  config->resourcePool[j].hostname);
	rc = 0;

	rc = ncAttachVolumeStub(ncs, ccMeta, instanceId, volumeId, remoteDev, localDev);
	if (!rc) {
	  ret = 0;
	} else {
	  ret = 1;
	}
	exit(ret);
      } else {
	rc = timewait(pid, &status, minint(op_timer / ((stop-start) - (j - start)), OP_TIMEOUT_PERNODE));
	op_timer = OP_TIMEOUT - (time(NULL) - op_start);
	rc = WEXITSTATUS(status);
	logprintfl(EUCADEBUG,"\tcall complete (pid/rc): %d/%d\n", pid, rc);
      }
    }
    sem_post(configLock);
    
    if (!rc) {
      ret = 0;
    } else {
      logprintfl(EUCAERROR, "failed to attach volume '%s'\n", instanceId);
      ret = 1;
    }
  }
  
  
  logprintfl(EUCADEBUG,"AttachVolume(): done.\n");
  
  shawn(); 
  return(ret);
}

int doDetachVolume(ncMetadata *ccMeta, char *volumeId, char *instanceId, char *remoteDev, char *localDev, int force) {
  int i, j, rc, start, stop, ret=0;
  ccInstance *myInstance;
  ncStub *ncs;
  time_t op_start, op_timer;
  
  i = j = 0;
  myInstance = NULL;
  op_start = time(NULL);
  op_timer = OP_TIMEOUT;
  
  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG,"DetachVolume(): called\n");
  if (!volumeId || !instanceId || !remoteDev || !localDev) {
    logprintfl(EUCAERROR, "bad input params to DetachVolume(): volumeId=%s, instanceId=%s, remoteDev=%s, localDev=%s\n", volumeId ? volumeId : "UNSET", instanceId ? instanceId : "UNSET", remoteDev ? remoteDev : "UNSET", localDev ? localDev : "UNSET");
    return(1);
  }
  
  rc = find_instanceCacheId(instanceId, &myInstance);
  if (!rc) {
    // found the instance in the cache
    if (myInstance) {
      start = myInstance->ncHostIdx;
      stop = start+1;
      free(myInstance);
    }
  } else {
    start = 0;
    stop = config->numResources;
  }
  
  sem_wait(configLock);
  for (j=start; j<stop; j++) {
    // read the instance ids
    logprintfl(EUCAINFO,"DetachVolume(): calling dettach volume (%s) on (%s)\n", instanceId, config->resourcePool[j].hostname);
    if (1) {
      int pid, status;
      pid = fork();
      if (pid == 0) {
	ret=0;
	ncs = ncStubCreate(config->resourcePool[j].ncURL, NULL, NULL);
	if (config->use_wssec) {
	  rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	}
	logprintfl(EUCADEBUG, "calling detachVol on NC: %s\n",  config->resourcePool[j].hostname);
	rc = 0;
	rc = ncDetachVolumeStub(ncs, ccMeta, instanceId, volumeId, remoteDev, localDev, force);
	if (!rc) {
	  ret = 0;
	} else {
	  ret = 1;
	}
	exit(ret);
      } else {
	op_timer = OP_TIMEOUT - (time(NULL) - op_start);
	rc = timewait(pid, &status, minint(op_timer / ((stop-start) - (j - start)), OP_TIMEOUT_PERNODE));
	rc = WEXITSTATUS(status);
	logprintfl(EUCADEBUG,"\tcall complete (pid/rc): %d/%d\n", pid, rc);
      }
    }
    sem_post(configLock);
    
    if (!rc) {
      ret = 0;
    } else {
      logprintfl(EUCAERROR, "failed to dettach volume '%s'\n", instanceId);
      ret = 1;
    }
  }
  
  logprintfl(EUCADEBUG,"DetachVolume(): done.\n");
  
  shawn();
  
  return(ret);
}

int doConfigureNetwork(ncMetadata *meta, char *type, int namedLen, char **sourceNames, char **userNames, int netLen, char **sourceNets, char *destName, char *destUserName, char *protocol, int minPort, int maxPort) {
  int rc, i, fail;

  rc = initialize();
  if (rc) {
    return(1);
  }
  
  logprintfl(EUCADEBUG, "ConfigureNetwork(): called\n");
  
  if (!strcmp(vnetconfig->mode, "SYSTEM") || !strcmp(vnetconfig->mode, "STATIC")) {
    fail = 0;
  } else {
    
    if (destUserName == NULL) {
      destUserName = meta->userId;
    }
    
    sem_wait(vnetConfigLock);
    
    fail=0;
    for (i=0; i<namedLen; i++) {
      if (sourceNames && userNames) {
	rc = vnetTableRule(vnetconfig, type, destUserName, destName, userNames[i], NULL, sourceNames[i], protocol, minPort, maxPort);
      }
      if (rc) {
	logprintfl(EUCAERROR,"ERROR: vnetTableRule() returned error\n");
	fail=1;
      }
    }
    for (i=0; i<netLen; i++) {
      if (sourceNets) {
	rc = vnetTableRule(vnetconfig, type, destUserName, destName, NULL, sourceNets[i], NULL, protocol, minPort, maxPort);
      }
      if (rc) {
	logprintfl(EUCAERROR,"ERROR: vnetTableRule() returned error\n");
	fail=1;
      }
    }
    sem_post(vnetConfigLock);
  }
  
  logprintfl(EUCADEBUG,"ConfigureNetwork(): done\n");
  
  if (fail) {
    return(1);
  }
  return(0);
}

int doFlushNetwork(ncMetadata *ccMeta, char *destName) {
  int rc;

  if (!strcmp(vnetconfig->mode, "SYSTEM") || !strcmp(vnetconfig->mode, "STATIC")) {
    return(0);
  }

  sem_wait(vnetConfigLock);
  rc = vnetFlushTable(vnetconfig, ccMeta->userId, destName);
  sem_post(vnetConfigLock);
  return(rc);
}

int doAssignAddress(ncMetadata *ccMeta, char *src, char *dst) {
  int rc, allocated, addrdevno, ret;
  char cmd[256];
  ccInstance *myInstance=NULL;

  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG,"AssignAddress(): called\n");

  if (!src || !dst || !strcmp(src, "0.0.0.0") || !strcmp(dst, "0.0.0.0")) {
    logprintfl(EUCADEBUG, "bad input params to AssignAddress(): src=%s, dst=%s\n", src ? src : "UNSET", dst ? dst : "UNSET");
    return(1);
  }
  
  ret = 0;
  
  if (!strcmp(vnetconfig->mode, "SYSTEM") || !strcmp(vnetconfig->mode, "STATIC")) {
    ret = 0;
  } else {
    
    sem_wait(vnetConfigLock);
    rc = vnetGetPublicIP(vnetconfig, src, NULL, &allocated, &addrdevno);
    if (rc) {
      logprintfl(EUCAERROR,"failed to get publicip record %s\n", src);
      ret = 1;
    } else {
      if (!allocated) {
	snprintf(cmd, 255, "%s/usr/lib/eucalyptus/euca_rootwrap ip addr add %s/32 dev %s", config->eucahome, src, vnetconfig->pubInterface);
	logprintfl(EUCAINFO,"running cmd %s\n", cmd);
	rc = system(cmd);
	rc = rc>>8;
	if (rc && (rc != 2)) {
	  logprintfl(EUCAERROR,"cmd '%s' failed\n", cmd);
	  ret = 1;
	} else {
	  rc = vnetAssignAddress(vnetconfig, src, dst);
	  if (rc) {
	    logprintfl(EUCAERROR,"could not assign address\n");
	    ret = 1;
	  } else {
	    rc = vnetAllocatePublicIP(vnetconfig, src, dst);
	    if (rc) {
	      logprintfl(EUCAERROR,"could not allocate public IP\n");
	      ret = 1;
	    }
	  }
	}
      } else {
	logprintfl(EUCAWARN,"ip %s is allready assigned, ignoring\n", src);
	ret = 0;
      }
    }
    sem_post(vnetConfigLock);
  }
  
  if (!ret) {
    // everything worked, update instance cache
    rc = find_instanceCacheIP(dst, &myInstance);
    if (!rc) {
      snprintf(myInstance->ccnet.publicIp, 24, "%s", src);
      rc = refresh_instanceCache(myInstance->instanceId, myInstance);
      free(myInstance);
    }
  }
  logprintfl(EUCADEBUG,"AssignAddress(): done\n");  
  return(ret);
}

int doDescribePublicAddresses(ncMetadata *ccMeta, publicip **outAddresses, int *outAddressesLen) {
  int rc;
  
  rc = initialize();
  if (rc) {
    return(1);
  }
  
  if (!strcmp(vnetconfig->mode, "MANAGED") || !strcmp(vnetconfig->mode, "MANAGED-NOVLAN")) {
    *outAddresses = vnetconfig->publicips;
    *outAddressesLen = NUMBER_OF_PUBLIC_IPS;
  } else {
    *outAddresses = NULL;
    *outAddressesLen = 0;
    return(2);
  }
  
  return(0);
}

int doUnassignAddress(ncMetadata *ccMeta, char *src, char *dst) {
  int rc, allocated, addrdevno, ret;
  char cmd[256];
  ccInstance *myInstance=NULL;

  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG,"UnassignAddress(): called\n");  
  
  if (!src || !dst || !strcmp(src, "0.0.0.0") || !strcmp(dst, "0.0.0.0")) {
    logprintfl(EUCADEBUG, "bad input params to UnassignAddress(): src=%s, dst=%s\n", src ? src : "UNSET", dst ? dst : "UNSET");
    return(1);
  }

  if (!strcmp(vnetconfig->mode, "SYSTEM") || !strcmp(vnetconfig->mode, "STATIC")) {
    ret = 0;
  } else {
    
    sem_wait(vnetConfigLock);
    ret=0;
    rc = vnetGetPublicIP(vnetconfig, src, NULL, &allocated, &addrdevno);
    if (rc) {
      logprintfl(EUCAERROR,"failed to find publicip to unassign (%s)\n", src);
      ret=1;
    } else {
      if (allocated && dst) {
	rc = vnetUnassignAddress(vnetconfig, src, dst); 
	if (rc) {
	  logprintfl(EUCAWARN,"vnetUnassignAddress() failed %d: %s/%s\n", rc, src, dst);
	}
	
	rc = vnetDeallocatePublicIP(vnetconfig, src, dst);
	if (rc) {
	  logprintfl(EUCAWARN,"vnetDeallocatePublicIP() failed %d: %s\n", rc, src);
	}
      }
      

      snprintf(cmd, 256, "%s/usr/lib/eucalyptus/euca_rootwrap ip addr del %s/32 dev %s", config->eucahome, src, vnetconfig->pubInterface);
      logprintfl(EUCADEBUG, "running cmd '%s'\n", cmd);
      rc = system(cmd);
      if (rc) {
      	logprintfl(EUCAWARN,"cmd failed '%s'\n", cmd);
      }
    }
    sem_post(vnetConfigLock);
  }

  if (!ret) {
    // refresh instance cache
    rc = find_instanceCacheIP(src, &myInstance);
    if (!rc) {
      snprintf(myInstance->ccnet.publicIp, 24, "0.0.0.0");
      rc = refresh_instanceCache(myInstance->instanceId, myInstance);
      free(myInstance);
    }
  }
  
  logprintfl(EUCADEBUG,"UnassignAddress(): done\n");  
  return(ret);
}

int doStopNetwork(ncMetadata *ccMeta, char *netName, int vlan) {
  int rc, ret;
  
  rc = initialize();
  if (rc) {
    return(1);
  }
  
  logprintfl(EUCADEBUG,"StopNetwork(): called\n");
  
  if (!ccMeta || !netName || vlan < 0) {
    logprintfl(EUCAERROR, "bad input params to StopNetwork(): ccMeta=%s, netName=%s, vlan=%d\n", ccMeta ? "SET" : "UNSET", netName ? netName : "UNSET", vlan);
  }

  logprintfl(EUCADEBUG, "\t vlan:%d\n", vlan);

  if (!strcmp(vnetconfig->mode, "SYSTEM") || !strcmp(vnetconfig->mode, "STATIC")) {
    ret = 0;
  } else {
    
    sem_wait(vnetConfigLock);
    if(ccMeta != NULL)
      rc = vnetStopNetwork(vnetconfig, vlan, ccMeta->userId, netName);
    ret = rc;
    sem_post(vnetConfigLock);
  }
  
  logprintfl(EUCADEBUG,"StopNetwork(): done\n");
  
  return(ret);
}

int doDescribeNetworks(ncMetadata *ccMeta, char *nameserver, char **ccs, int ccsLen, vnetConfig *outvnetConfig) {
  int rc, i, j;
  
  rc = initialize();
  if (rc) {
    return(1);
  }

  logprintfl(EUCADEBUG, "DescribeNetworks(): called\n");
  
  sem_wait(vnetConfigLock);
  if (nameserver) {
    vnetconfig->euca_ns = dot2hex(nameserver);
  }
  if (!strcmp(vnetconfig->mode, "MANAGED") || !strcmp(vnetconfig->mode, "MANAGED-NOVLAN")) {
    rc = vnetSetCCS(vnetconfig, ccs, ccsLen);
    rc = vnetSetupTunnels(vnetconfig);
  }
  memcpy(outvnetConfig, vnetconfig, sizeof(vnetConfig));

  sem_post(vnetConfigLock);
  logprintfl(EUCADEBUG, "DescribeNetworks(): done\n");
  
  shawn();
  return(0);
}

int doStartNetwork(ncMetadata *ccMeta, char *netName, int vlan, char *nameserver, char **ccs, int ccsLen) {
  int rc, ret;
  time_t op_start, op_timer;
  char *brname;
  
  op_start = time(NULL);
  op_timer = OP_TIMEOUT;

  rc = initialize();
  if (rc) {
    return(1);
  }
  
  logprintfl(EUCADEBUG, "StartNetwork(): called\n");
  logprintfl(EUCADEBUG, "\t vlan:%d\n", vlan);
  if (!strcmp(vnetconfig->mode, "SYSTEM") || !strcmp(vnetconfig->mode, "STATIC")) {
    ret = 0;
  } else {
    sem_wait(vnetConfigLock);
    if (nameserver) {
      vnetconfig->euca_ns = dot2hex(nameserver);
    }
    
    rc = vnetSetCCS(vnetconfig, ccs, ccsLen);
    rc = vnetSetupTunnels(vnetconfig);

    brname = NULL;
    rc = vnetStartNetwork(vnetconfig, vlan, ccMeta->userId, netName, &brname);
    if (brname) free(brname);

    sem_post(vnetConfigLock);
    
    if (rc) {
      logprintfl(EUCAERROR,"StartNetwork(): ERROR return from vnetStartNetwork %d\n", rc);
      ret = 1;
    } else {
      logprintfl(EUCAINFO,"StartNetwork(): SUCCESS return from vnetStartNetwork %d\n", rc);
      ret = 0;
    }
    
  }
  
  logprintfl(EUCADEBUG,"StartNetwork(): done\n");
  
  shawn();
  
  return(ret);
}

int doDescribeResources(ncMetadata *ccMeta, virtualMachine **ccvms, int vmLen, int **outTypesMax, int **outTypesAvail, int *outTypesLen, char ***outServiceTags, int *outServiceTagsLen) {
  int i;
  int rc, diskpool, mempool, corepool;
  int j;
  resource *res;
  time_t op_start, op_timer;

  op_start = time(NULL);
  op_timer = OP_TIMEOUT;

  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG,"DescribeResources(): called %d\n", vmLen);
  
  if (outTypesMax == NULL || outTypesAvail == NULL || outTypesLen == NULL || outServiceTags == NULL || outServiceTagsLen == NULL) {
    // input error
    return(1);
  }
  
  print_instanceCache();

  *outServiceTags = malloc(sizeof(char *) * config->numResources);
  if (*outServiceTags == NULL) {
      *outServiceTagsLen = 0;
       logprintfl(EUCAWARN,"cannot allocate outServiceTags\n");
  } else {
      *outServiceTagsLen = config->numResources;
      for (i=0; i<config->numResources; i++) {
        (*outServiceTags)[i] = strdup(config->resourcePool[i].ncURL);
        if ((*outServiceTags)[i] == NULL) 
           logprintfl(EUCAWARN,"not enough memory fot outServiceTags[%d]\n", i);
      }
  }
  
  *outTypesMax = NULL;
  *outTypesAvail = NULL;
  
  *outTypesMax = malloc(sizeof(int) * vmLen);
  *outTypesAvail = malloc(sizeof(int) * vmLen);
  if (*outTypesMax == NULL || *outTypesAvail == NULL) {
      logprintfl(EUCAERROR,"DescribeResources(): out of memory\n");
      if (*outTypesAvail) free(*outTypesAvail);
      if (*outTypesMax) free(*outTypesMax);
      *outTypesLen = 0;
      if (*outServiceTags) {
         for (i=0; i < config->numResources; i++) 
            if ((*outServiceTags)[i]) free((*outServiceTags)[i]);
         free(*outServiceTags);
      }
      *outServiceTags = NULL;
      *outServiceTagsLen = 0;
      return(1);
  }
  bzero(*outTypesMax, sizeof(int) * vmLen);
  bzero(*outTypesAvail, sizeof(int) * vmLen);

  *outTypesLen = vmLen;

  for (i=0; i<vmLen; i++) {
    if ((*ccvms)[i].mem <= 0 || (*ccvms)[i].cores <= 0 || (*ccvms)[i].disk <= 0) {
      logprintfl(EUCAERROR,"DescribeResources(): input error\n");
      if (*outTypesAvail) free(*outTypesAvail);
      if (*outTypesMax) free(*outTypesMax);
      *outTypesLen = 0;
      if (*outServiceTags) {
         for (i=0; i < config->numResources; i++) 
            if ((*outServiceTags)[i]) free((*outServiceTags)[i]);
         free(*outServiceTags);
      }
      *outServiceTags = NULL;
      *outServiceTagsLen = 0;
      return(1);
    }
  }

  sem_wait(configLock);
  {
    for (i=0; i<config->numResources; i++) {
      res = &(config->resourcePool[i]);
      
      for (j=0; j<vmLen; j++) {
	mempool = res->availMemory;
	diskpool = res->availDisk;
	corepool = res->availCores;
	
	mempool -= (*ccvms)[j].mem;
	diskpool -= (*ccvms)[j].disk;
	corepool -= (*ccvms)[j].cores;
	while (mempool >= 0 && diskpool >= 0 && corepool >= 0) {
	  (*outTypesAvail)[j]++;
	  mempool -= (*ccvms)[j].mem;
	  diskpool -= (*ccvms)[j].disk;
	  corepool -= (*ccvms)[j].cores;
	}
	
	mempool = res->maxMemory;
	diskpool = res->maxDisk;
	corepool = res->maxCores;
	
	mempool -= (*ccvms)[j].mem;
	diskpool -= (*ccvms)[j].disk;
	corepool -= (*ccvms)[j].cores;
	while (mempool >= 0 && diskpool >= 0 && corepool >= 0) {
	  (*outTypesMax)[j]++;
	  mempool -= (*ccvms)[j].mem;
	  diskpool -= (*ccvms)[j].disk;
	  corepool -= (*ccvms)[j].cores;
	}
      }
    }
    sem_post(configLock);
  }
  logprintfl(EUCADEBUG,"DescribeResources(): resources %d/%d %d/%d %d/%d %d/%d %d/%d\n", (*outTypesAvail)[0], (*outTypesMax)[0], (*outTypesAvail)[1], (*outTypesMax)[1], (*outTypesAvail)[2], (*outTypesMax)[2], (*outTypesAvail)[3], (*outTypesMax)[3], (*outTypesAvail)[4], (*outTypesMax)[4]);

  logprintfl(EUCADEBUG,"DescribeResources(): done\n");
  
  shawn();
  return(0);
}

int changeState(resource *in, int newstate) {
  if (in == NULL) return(1);
  if (in->state == newstate) return(0);
  
  in->lastState = in->state;
  in->state = newstate;
  in->stateChange = time(NULL);
  in->idleStart = 0;
  
  return(0);
}

int refresh_resources(ncMetadata *ccMeta, int timeout, int dolock) {
  int i, rc;
  int pid, status, ret=0;
  int filedes[2];  
  time_t op_start, op_timer, op_pernode;
  ncStub *ncs;
  ncResource *ncRes;

  if (timeout <= 0) timeout = 1;

  op_start = time(NULL);
  op_timer = timeout;
  logprintfl(EUCADEBUG,"refresh_resources(): called\n");

  if (dolock) {
    sem_wait(configLock);
  }

  for (i=0; i<config->numResources; i++) {
    if (config->resourcePool[i].state != RESASLEEP) {
      rc = pipe(filedes);
      logprintfl(EUCADEBUG, "calling %s\n", config->resourcePool[i].ncURL);
      pid = fork();
      if (pid == 0) {
	ret=0;
	close(filedes[0]);
	ncs = ncStubCreate(config->resourcePool[i].ncURL, NULL, NULL);
	if (config->use_wssec) {
	  rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	}
	rc = ncDescribeResourceStub(ncs, ccMeta, NULL, &ncRes);
	if (!rc) {
	  rc = write(filedes[1], ncRes, sizeof(ncResource));
	  ret = 0;
	} else {
	  ret = 1;
	}
	close(filedes[1]);
	exit(ret);
      } else {
	close(filedes[1]);
	ncRes = malloc(sizeof(ncResource));
	if (!ncRes) {
	  logprintfl(EUCAERROR, "refresh_resources: out of memory\n");
	  kill(pid, SIGKILL);
	  wait(&status);
	  rc = 1;
	} else {
	  bzero(ncRes, sizeof(ncResource));
	  op_timer = timeout - (time(NULL) - op_start);
	  op_pernode = (op_timer/(config->numResources-i)) > OP_TIMEOUT_PERNODE ? (op_timer/(config->numResources-i)) : OP_TIMEOUT_PERNODE;
	  logprintfl(EUCADEBUG, "\ttime left for next op: %d\n", op_pernode);
	  rc = timeread(filedes[0], ncRes, sizeof(ncResource), op_pernode);
	  close(filedes[0]);
	  if (rc <= 0) {
	    // timeout or read went badly
	    kill(pid, SIGKILL);
	    wait(&status);
	    rc = 1;
	  } else {
	    wait(&status);
	    rc = WEXITSTATUS(status);
	  }
	}
      }
      
      if (rc != 0) {
	powerUp(&(config->resourcePool[i]));
	
	if (config->resourcePool[i].state == RESWAKING && ((time(NULL) - config->resourcePool[i].stateChange) < config->wakeThresh)) {
	  logprintfl(EUCAINFO, "resource still waking up (%d more seconds until marked as down)\n", config->wakeThresh - (time(NULL) - config->resourcePool[i].stateChange));
	} else{
	  logprintfl(EUCAERROR,"bad return from ncDescribeResource(%s) (%d/%d)\n", config->resourcePool[i].hostname, pid, rc);
	  config->resourcePool[i].maxMemory = 0;
	  config->resourcePool[i].availMemory = 0;
	  config->resourcePool[i].maxDisk = 0;
	  config->resourcePool[i].availDisk = 0;
	  config->resourcePool[i].maxCores = 0;
	  config->resourcePool[i].availCores = 0;    
	  changeState(&(config->resourcePool[i]), RESDOWN);
	}
      } else {
	logprintfl(EUCAINFO,"\tnode=%s mem=%d/%d disk=%d/%d cores=%d/%d\n", config->resourcePool[i].hostname, ncRes->memorySizeMax, ncRes->memorySizeAvailable, ncRes->diskSizeMax,  ncRes->diskSizeAvailable, ncRes->numberOfCoresMax, ncRes->numberOfCoresAvailable);
	config->resourcePool[i].maxMemory = ncRes->memorySizeMax;
	config->resourcePool[i].availMemory = ncRes->memorySizeAvailable;
	config->resourcePool[i].maxDisk = ncRes->diskSizeMax;
	config->resourcePool[i].availDisk = ncRes->diskSizeAvailable;
	config->resourcePool[i].maxCores = ncRes->numberOfCoresMax;
	config->resourcePool[i].availCores = ncRes->numberOfCoresAvailable;    
	changeState(&(config->resourcePool[i]), RESUP);
	if (ncRes) free(ncRes);
      }
    } else {
      logprintfl(EUCADEBUG, "resource asleep, skipping resource update\n");
    }

    // try to discover the mac address of the resource
    if (config->resourcePool[i].mac[0] == '\0' && config->resourcePool[i].ip[0] != '\0') {
      char *mac;
      rc = ip2mac(vnetconfig, config->resourcePool[i].ip, &mac);
      if (!rc) {
	strncpy(config->resourcePool[i].mac, mac, 24);
	free(mac);
	logprintfl(EUCADEBUG, "discovered MAC '%s' for host %s(%s)\n", config->resourcePool[i].mac, config->resourcePool[i].hostname, config->resourcePool[i].ip);
      }
    }
  }
  if (dolock) {
    sem_post(configLock);
  }
  
  logprintfl(EUCADEBUG,"refresh_resources(): done\n");
  return(0);
}

int refresh_instances(ncMetadata *ccMeta, int timeout, int dolock) {
  ccInstance *myInstance=NULL, *cacheInstance=NULL;
  int i, k, numInsts = 0, found, ncOutInstsLen, rc, pid;
  virtualMachine ccvm;
  time_t op_start, op_timer, op_pernode;

  ncInstance **ncOutInsts=NULL;
  ncStub *ncs;
  
  op_start = time(NULL);
  op_timer = timeout;
  logprintfl(EUCADEBUG,"refresh_instances(): called\n");

  if (dolock) {
    sem_wait(configLock);  
  }

  invalidate_instanceCache();

  for (i=0; i<config->numResources; i++) {
    if (config->resourcePool[i].state == RESUP) {
      int status, ret=0;
      int filedes[2];
      int len, j;
      
      rc = pipe(filedes);
      pid = fork();
      if (pid == 0) {
	ret=0;
	close(filedes[0]);
	ncs = ncStubCreate(config->resourcePool[i].ncURL, NULL, NULL);
	if (config->use_wssec) {
	  rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	}
	ncOutInstsLen=0;

	rc = ncDescribeInstancesStub(ncs, ccMeta, NULL, 0, &ncOutInsts, &ncOutInstsLen);

	if (!rc) {
	  len = ncOutInstsLen;
	  rc = write(filedes[1], &len, sizeof(int));
	  for (j=0; j<len; j++) {
	    ncInstance *inst;
	    inst = ncOutInsts[j];
	    rc = write(filedes[1], inst, sizeof(ncInstance));
	  }
	  ret = 0;
	} else {
	  len = 0;
	  rc = write(filedes[1], &len, sizeof(int));
	  ret = 1;
	}
	close(filedes[1]);
	fflush(stdout);
	
	exit(ret);
      } else {
	int len,rbytes,j;
	ncInstance *inst;
	close(filedes[1]);
	
	op_timer = OP_TIMEOUT - (time(NULL) - op_start);
	op_pernode = (op_timer/(config->numResources-i)) > OP_TIMEOUT_PERNODE ? (op_timer/(config->numResources-i)) : OP_TIMEOUT_PERNODE;
	logprintfl(EUCADEBUG, "\ttimeout(%d/%d) len\n", op_pernode, OP_TIMEOUT);
	rbytes = timeread(filedes[0], &len, sizeof(int), op_pernode);
	if (rbytes <= 0) {
	  // read went badly
	  kill(pid, SIGKILL);
	  wait(&status);
	  rc = -1;
	} else {
	  if (rbytes < sizeof(int)) {
	    len = 0;
	    ncOutInsts = NULL;
	    ncOutInstsLen = 0;
	  } else {
	    ncOutInsts = malloc(sizeof(ncInstance *) * len);
	    ncOutInstsLen = len;
	    for (j=0; j<len; j++) {
	      inst = malloc(sizeof(ncInstance));
	      op_timer = OP_TIMEOUT - (time(NULL) - op_start);
	      op_pernode = (op_timer/(config->numResources-i)) > OP_TIMEOUT_PERNODE ? (op_timer/(config->numResources-i)) : OP_TIMEOUT_PERNODE;
	      logprintfl(EUCADEBUG, "\ttimeout(%d/%d) inst\n", op_pernode, OP_TIMEOUT);
	      rbytes = timeread(filedes[0], inst, sizeof(ncInstance), op_pernode);
	      ncOutInsts[j] = inst;
	    }
	  }
	  wait(&status);
	  rc = WEXITSTATUS(status);
	  
	  // power down
	  if (rc == 0 && len == 0) {
	    logprintfl(EUCADEBUG, "node %s idle since %d: (%d/%d) seconds\n", config->resourcePool[i].hostname, config->resourcePool[i].idleStart, time(NULL) - config->resourcePool[i].idleStart, config->idleThresh); 
	    if (!config->resourcePool[i].idleStart) {
	      config->resourcePool[i].idleStart = time(NULL);
	    } else if ((time(NULL) - config->resourcePool[i].idleStart) > config->idleThresh) {
	      // call powerdown
	      
	      if (powerDown(ccMeta, &(config->resourcePool[i]))) {
		logprintfl(EUCAWARN, "powerDown for %s failed\n", config->resourcePool[i].hostname);
	      }
	    }
	  } else {
	    config->resourcePool[i].idleStart = 0;
	  }
	}
	close(filedes[0]);
      }
      
      if (rc != 0) {
	logprintfl(EUCAERROR,"ncDescribeInstancesStub(%s): returned fail: (%d/%d)\n", config->resourcePool[i].ncURL, pid, rc);
      } else {
	for (j=0; j<ncOutInstsLen; j++) {
	  found=1;
	  if (found) {
	    // add it
	    logprintfl(EUCAINFO,"DescribeInstances(): describing instance %s, %s, %d\n", ncOutInsts[j]->instanceId, ncOutInsts[j]->stateName, j);
	    numInsts++;
	    
	    // ccvm.name = TODO
	    bzero(ccvm.name, 64);
	    ccvm.mem = ncOutInsts[j]->params.memorySize;
	    ccvm.disk = ncOutInsts[j]->params.diskSize;
	    ccvm.cores = ncOutInsts[j]->params.numberOfCores;
	    
	    myInstance = malloc(sizeof(ccInstance));
	    bzero(myInstance, sizeof(ccInstance));

	    myInstance->networkIndex = -1;
	    
	    //cacheInstance=NULL;
	    /*
	    if (!find_instanceCacheId(ncOutInsts[j]->instanceId, &cacheInstance)) {
	      logprintfl(EUCADEBUG, "\t%s in cache\n", ncOutInsts[j]->instanceId);
	      memcpy(myInstance, cacheInstance, sizeof(ccInstance));
	    }
	    */
	    
	    rc = ccInstance_to_ncInstance(myInstance, ncOutInsts[j]);
	    // instance info that the CC maintains
	    myInstance->ncHostIdx = i;
	    strncpy(myInstance->serviceTag, config->resourcePool[i].ncURL, 64);
	    memcpy(&(myInstance->ccvm), &ccvm, sizeof(virtualMachine));
	    
	    {
	      char *ip;
	      if (!strcmp(myInstance->ccnet.publicIp, "0.0.0.0")) {
		if (!strcmp(vnetconfig->mode, "SYSTEM") || !strcmp(vnetconfig->mode, "STATIC")) {
		  rc = mac2ip(vnetconfig, myInstance->ccnet.publicMac, &ip);
		  if (!rc) {
		    strncpy(myInstance->ccnet.publicIp, ip, 24);
		    free(ip);
		  }
		}
	      }
	      if (!strcmp(myInstance->ccnet.privateIp, "0.0.0.0")) {
		rc = mac2ip(vnetconfig, myInstance->ccnet.privateMac, &ip);
		if (!rc) {
		  strncpy(myInstance->ccnet.privateIp, ip, 24);
		  free(ip);
		}
	      }
	    }
	//    if (cacheInstance) free(cacheInstance);
	    refresh_instanceCache(myInstance->instanceId, myInstance);
	    logprintfl(EUCADEBUG, "returning instance state: %s/%s\n", myInstance->instanceId, myInstance->state);
	    free(myInstance);
	  }
	}
      }
      if (ncOutInsts) {
        for (j=0; j<ncOutInstsLen; j++) {
          free_instance(&(ncOutInsts[j]));
        }
        free(ncOutInsts);
        ncOutInsts = NULL;
      }
    }
  }
  if (dolock) {
    sem_post(configLock);
  }
  logprintfl(EUCADEBUG,"refresh_instances(): done\n");  
  return(0);
}

int doDescribeInstances(ncMetadata *ccMeta, char **instIds, int instIdsLen, ccInstance **outInsts, int *outInstsLen) {
  ccInstance *myInstance=NULL, *out=NULL, *cacheInstance=NULL;
  int i, k, numInsts, found, ncOutInstsLen, rc, pid;
  virtualMachine ccvm;
  time_t op_start, op_timer;

  ncInstance **ncOutInsts=NULL;
  ncStub *ncs;
  
  op_start = time(NULL);
  op_timer = OP_TIMEOUT;

  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG, "printing instance cache in describeInstances()\n");
  print_instanceCache();

  logprintfl(EUCADEBUG,"DescribeInstances(): called\n");
  
  *outInsts = NULL;
  *outInstsLen = 0;

  sem_wait(instanceCacheLock);
  *outInsts = malloc(sizeof(ccInstance) * instanceCache->numInsts);
  memcpy(*outInsts, instanceCache->instances, sizeof(ccInstance) * instanceCache->numInsts);
  *outInstsLen = instanceCache->numInsts;
  sem_post(instanceCacheLock);

  logprintfl(EUCADEBUG,"DescribeInstances(): done\n");

  shawn();
      
  return(0);
}

int powerUp(resource *res) {
  int rc,ret,len, i;
  char cmd[256], *bc=NULL;
  uint32_t *ips=NULL, *nms=NULL;
  
  if (config->schedPolicy != SCHEDPOWERSAVE) {
    return(0);
  }

  rc = getdevinfo(vnetconfig->privInterface, &ips, &nms, &len);
  if (rc) {
    ips = malloc(sizeof(uint32_t));
    nms = malloc(sizeof(uint32_t));
    ips[0] = 0xFFFFFFFF;
    nms[0] = 0xFFFFFFFF;
    len = 1;
  }
  
  for (i=0; i<len; i++) {
    logprintfl(EUCADEBUG, "attempting to wake up resource %s(%s/%s)\n", res->hostname, res->ip, res->mac);
    // try to wake up res

    // broadcast
    bc = hex2dot((0xFFFFFFFF - nms[i]) | (ips[i] & nms[i]));

    rc = 0;
    ret = 0;
    if (strcmp(res->mac, "00:00:00:00:00:00")) {
      snprintf(cmd, 256, "%s/usr/lib/eucalyptus/euca_rootwrap powerwake -b %s %s", vnetconfig->eucahome, bc, res->mac);
    } else if (strcmp(res->ip, "0.0.0.0")) {
      snprintf(cmd, 256, "%s/usr/lib/eucalyptus/euca_rootwrap powerwake -b %s %s", vnetconfig->eucahome, bc, res->ip);
    } else {
      ret = rc = 1;
    }
    if (bc) free(bc);
    if (!rc) {
      logprintfl(EUCADEBUG, "waking up powered off host %s(%s/%s): %s\n", res->hostname, res->ip, res->mac, cmd);
      rc = system(cmd);
      rc = rc>>8;
      if (rc) {
	logprintfl(EUCAERROR, "cmd failed: %d\n", rc);
	ret = 1;
      } else {
	logprintfl(EUCAERROR, "cmd success: %d\n", rc);
	changeState(res, RESWAKING);
	ret = 0;
      }
    }
  }
  if (ips) free(ips);
  if (nms) free(nms);
  return(ret);
}

int powerDown(ncMetadata *ccMeta, resource *node) {
  int pid, rc, status;
  ncStub *ncs=NULL;
  time_t op_start, op_timer;
  
  if (config->schedPolicy != SCHEDPOWERSAVE) {
    node->idleStart = 0;
    return(0);
  }

  op_start = time(NULL);
  op_timer = OP_TIMEOUT;
  
  logprintfl(EUCADEBUG, "sending powerdown to node: %s, %s\n", node->hostname, node->ncURL);
  
  pid = fork();
  if (pid == 0) {
    ncs = ncStubCreate(node->ncURL, NULL, NULL);
    if (config->use_wssec) {
      rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
    }
    rc = ncPowerDownStub(ncs, ccMeta);
    exit(rc);
  }
  op_timer = OP_TIMEOUT - (time(NULL) - op_start);
  rc = timewait(pid, &status, minint(op_timer, OP_TIMEOUT_PERNODE));
  rc = WEXITSTATUS(status);
  if (rc == 0) {
    changeState(node, RESASLEEP);
  }
  return(rc);
}

int ccInstance_to_ncInstance(ccInstance *dst, ncInstance *src) {
  int i;
  
  strncpy(dst->instanceId, src->instanceId, 16);
  strncpy(dst->reservationId, src->reservationId, 16);
  strncpy(dst->ownerId, src->userId, 16);
  strncpy(dst->amiId, src->imageId, 16);
  strncpy(dst->kernelId, src->kernelId, 16);
  strncpy(dst->ramdiskId, src->ramdiskId, 16);
  strncpy(dst->keyName, src->keyName, 1024);
  strncpy(dst->launchIndex, src->launchIndex, 64);
  strncpy(dst->userData, src->userData, 64);
  for (i=0; i < src->groupNamesSize && i < 64; i++) {
    snprintf(dst->groupNames[i], 32, "%s", src->groupNames[i]);
  }
  strncpy(dst->state, src->stateName, 16);
  dst->ccnet.vlan = src->ncnet.vlan;
  strncpy(dst->ccnet.publicMac, src->ncnet.publicMac, 24);
  strncpy(dst->ccnet.privateMac, src->ncnet.privateMac, 24);
  if (strcmp(src->ncnet.publicIp, "0.0.0.0") || dst->ccnet.publicIp[0] == '\0') strncpy(dst->ccnet.publicIp, src->ncnet.publicIp, 16);
  if (strcmp(src->ncnet.privateIp, "0.0.0.0") || dst->ccnet.privateIp[0] == '\0') strncpy(dst->ccnet.privateIp, src->ncnet.privateIp, 16);

  memcpy(dst->volumes, src->volumes, sizeof(ncVolume) * EUCA_MAX_VOLUMES);
  dst->volumesSize = src->volumesSize;

  return(0);
}

int schedule_instance(virtualMachine *vm, char *targetNode, int *outresid) {
  int ret;
  ncMetadata ccMeta;

  if (targetNode != NULL) {
    ret = schedule_instance_explicit(vm, targetNode, outresid);
  } else if (config->schedPolicy == SCHEDGREEDY) {
    ret = schedule_instance_greedy(vm, outresid);
  } else if (config->schedPolicy == SCHEDROUNDROBIN) {
    ret = schedule_instance_roundrobin(vm, outresid);
  } else if (config->schedPolicy == SCHEDPOWERSAVE) {
    ret = schedule_instance_greedy(vm, outresid);
  } else {
    ret = schedule_instance_greedy(vm, outresid);
  }

  return(ret);
}

int schedule_instance_roundrobin(virtualMachine *vm, int *outresid) {
  int i, done, start, found, resid=0;
  resource *res;

  *outresid = 0;

  logprintfl(EUCAINFO, "scheduler using ROUNDROBIN policy to find next resource\n");

  // find the best 'resource' on which to run the instance
  done=found=0;
  start = config->schedState;
  i = start;
  
  logprintfl(EUCADEBUG, "scheduler state starting at resource %d\n", config->schedState);
  while(!done) {
    int mem, disk, cores;
    
    res = &(config->resourcePool[i]);
    if (res->state != RESDOWN) {
      mem = res->availMemory - vm->mem;
      disk = res->availDisk - vm->disk;
      cores = res->availCores - vm->cores;
      
      if (mem >= 0 && disk >= 0 && cores >= 0) {
	resid = i;
	found=1;
	done++;
      }
    }
    i++;
    if (i >= config->numResources) {
      i = 0;
    }
    if (i == start) {
      done++;
    }
  }

  if (!found) {
    // didn't find a resource
    return(1);
  }

  *outresid = resid;
  config->schedState = i;
  logprintfl(EUCADEBUG, "scheduler state finishing at resource %d\n", config->schedState);

  return(0);
}

int schedule_instance_explicit(virtualMachine *vm, char *targetNode, int *outresid) {
  int i, rc, done, resid, sleepresid;
  resource *res;
  
  *outresid = 0;

  logprintfl(EUCAINFO, "scheduler using EXPLICIT policy to run VM on target node '%s'\n", targetNode);

  // find the best 'resource' on which to run the instance
  resid = sleepresid = -1;
  done=0;
  for (i=0; i<config->numResources && !done; i++) {
    int mem, disk, cores;
    
    // new fashion way
    res = &(config->resourcePool[i]);
    if (!strcmp(res->hostname, targetNode)) {
      done++;
      if (res->state == RESUP) {
	mem = res->availMemory - vm->mem;
	disk = res->availDisk - vm->disk;
	cores = res->availCores - vm->cores;
	
	if (mem >= 0 && disk >= 0 && cores >= 0) {
	  resid = i;
	}
      } else if (res->state == RESASLEEP) {
	mem = res->availMemory - vm->mem;
	disk = res->availDisk - vm->disk;
	cores = res->availCores - vm->cores;
	
	if (mem >= 0 && disk >= 0 && cores >= 0) {
	  sleepresid = i;
	}
      }
    }
  }
  
  if (resid == -1 && sleepresid == -1) {
    // target resource is unavailable
    return(1);
  }
  
  if (resid != -1) {
    res = &(config->resourcePool[resid]);
    *outresid = resid;
  } else if (sleepresid != -1) {
    res = &(config->resourcePool[sleepresid]);
    *outresid = sleepresid;
  }
  if (res->state == RESASLEEP) {
    rc = powerUp(res);
  }

  return(0);
}

int schedule_instance_greedy(virtualMachine *vm, int *outresid) {
  int i, rc, done, resid, sleepresid;
  resource *res;
  
  *outresid = 0;

  if (config->schedPolicy == SCHEDGREEDY) {
    logprintfl(EUCAINFO, "scheduler using GREEDY policy to find next resource\n");
  } else if (config->schedPolicy == SCHEDPOWERSAVE) {
    logprintfl(EUCAINFO, "scheduler using POWERSAVE policy to find next resource\n");
  }

  // find the best 'resource' on which to run the instance
  resid = sleepresid = -1;
  done=0;
  for (i=0; i<config->numResources && !done; i++) {
    int mem, disk, cores;
    
    // new fashion way
    res = &(config->resourcePool[i]);
    if ((res->state == RESUP || res->state == RESWAKING) && resid == -1) {
      mem = res->availMemory - vm->mem;
      disk = res->availDisk - vm->disk;
      cores = res->availCores - vm->cores;
      
      if (mem >= 0 && disk >= 0 && cores >= 0) {
	resid = i;
	done++;
      }
    } else if (res->state == RESASLEEP && sleepresid == -1) {
      mem = res->availMemory - vm->mem;
      disk = res->availDisk - vm->disk;
      cores = res->availCores - vm->cores;
      
      if (mem >= 0 && disk >= 0 && cores >= 0) {
	sleepresid = i;
      }
    }
  }
  
  if (resid == -1 && sleepresid == -1) {
    // didn't find a resource
    return(1);
  }
  
  if (resid != -1) {
    res = &(config->resourcePool[resid]);
    *outresid = resid;
  } else if (sleepresid != -1) {
    res = &(config->resourcePool[sleepresid]);
    *outresid = sleepresid;
  }
  if (res->state == RESASLEEP) {
    rc = powerUp(res);
  }

  return(0);
}

int doRunInstances(ncMetadata *ccMeta, char *amiId, char *kernelId, char *ramdiskId, char *amiURL, char *kernelURL, char *ramdiskURL, char **instIds, int instIdsLen, char **netNames, int netNamesLen, char **macAddrs, int macAddrsLen, int *networkIndexList, int networkIndexListLen, int minCount, int maxCount, char *ownerId, char *reservationId, virtualMachine *ccvm, char *keyName, int vlan, char *userData, char *launchIndex, char *targetNode, ccInstance **outInsts, int *outInstsLen) {
  int rc=0, i=0, done=0, runCount=0, resid=0, foundnet=0, error=0, networkIdx=0, nidx=0, thenidx=0;
  ccInstance *myInstance=NULL, 
    *retInsts=NULL;
  char *instId=NULL;
  time_t op_start=0, op_timer=0;
  resource *res=NULL;
  char mac[32], privip[32], pubip[32];

  ncInstance *outInst=NULL;
  ncInstParams ncvm;
  ncStub *ncs=NULL;
  
  op_start = time(NULL);
  op_timer = OP_TIMEOUT;
  
  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG,"RunInstances(): called\n");
  
  *outInstsLen = 0;
  
  if (!ccvm) {
    logprintfl(EUCAERROR,"RunInstances(): invalid ccvm\n");
    return(-1);
  }
  if (minCount <= 0 || maxCount <= 0 || instIdsLen < maxCount) {
    logprintfl(EUCAERROR,"RunInstances(): bad min or max count, or not enough instIds (%d, %d, %d)\n", minCount, maxCount, instIdsLen);
    return(-1);
  }

  // check health of the networkIndexList
  if ( (!strcmp(vnetconfig->mode, "SYSTEM") || !strcmp(vnetconfig->mode, "STATIC")) || networkIndexList == NULL) {
    // disabled
    nidx=-1;
  } else {
    if ( (networkIndexListLen < minCount) || (networkIndexListLen > maxCount) ) {
      logprintfl(EUCAERROR, "network index length (%d) is out of bounds for min/max instances (%d-%d)\n", networkIndexListLen, minCount, maxCount);
      return(1);
    }
    for (i=0; i<networkIndexListLen; i++) {
      if ( (networkIndexList[i] < 0) || (networkIndexList[i] > (vnetconfig->numaddrs-1)) ) {
	logprintfl(EUCAERROR, "network index (%d) out of bounds (0-%d)\n", networkIndexList[i], vnetconfig->numaddrs-1);
	return(1);
      }
    }

    // all checked out
    nidx=0;
  }
  
  retInsts = malloc(sizeof(ccInstance) * maxCount);  
  runCount=0;
  
  // get updated resource information

  done=0;
  for (i=0; i<maxCount && !done; i++) {
    instId = strdup(instIds[i]);
    logprintfl(EUCAINFO,"\trunning instance %s with emiId %s...\n", instId, amiId);
    
    // generate new mac
    bzero(mac, 32);
    bzero(pubip, 32);
    bzero(privip, 32);
    
    strncpy(pubip, "0.0.0.0", 32);
    strncpy(privip, "0.0.0.0", 32);
    if (macAddrsLen >= maxCount) {
      strncpy(mac, macAddrs[i], 32);
    }      

    sem_wait(vnetConfigLock);
    if (nidx == -1) {
      rc = vnetGenerateNetworkParams(vnetconfig, instId, vlan, -1, mac, pubip, privip);
      thenidx = -1;
    } else {
      rc = vnetGenerateNetworkParams(vnetconfig, instId, vlan, networkIndexList[nidx], mac, pubip, privip);
      thenidx=nidx;
      nidx++;
    }
    if (rc) {
      foundnet = 0;
    } else {
      foundnet = 1;
    }
    sem_post(vnetConfigLock);
    
    if (thenidx != -1) {
      logprintfl(EUCAINFO,"\tassigning MAC/IP: %s/%s/%s/%d\n", mac, pubip, privip, networkIndexList[thenidx]);
    } else {
      logprintfl(EUCAINFO,"\tassigning MAC/IP: %s/%s/%s/%d\n", mac, pubip, privip, thenidx);
    }
    
    if (mac[0] == '\0' || !foundnet) {
      logprintfl(EUCAERROR,"could not find/initialize any free network address, failing doRunInstances()\n");
    } else {
      // "run" the instance
      ncvm.memorySize = ccvm->mem;
      ncvm.diskSize = ccvm->disk;
      ncvm.numberOfCores = ccvm->cores;
      
      sem_wait(configLock);
      resid = 0;
      rc = schedule_instance(ccvm, targetNode, &resid);
      res = &(config->resourcePool[resid]);
      if (rc) {
	// could not find resource
	logprintfl(EUCAERROR, "scheduler could not find resource to run the instance on\n");
	// couldn't run this VM, remove networking information from system
	sem_wait(vnetConfigLock);
	
	vnetDisableHost(vnetconfig, mac, NULL, 0);
	if (!strcmp(vnetconfig->mode, "MANAGED") || !strcmp(vnetconfig->mode, "MANAGED-NOVLAN")) {
	  vnetDelHost(vnetconfig, mac, NULL, vlan);
	}
	
	sem_post(vnetConfigLock);
      } else {
	int pid, status, ret, rbytes;
	int filedes[2];
	
	// try to run the instance on the chosen resource
	logprintfl(EUCAINFO, "\tscheduler decided to run instance '%s' on resource '%s'\n", instId, res->ncURL);
	
	outInst=NULL;
	
	rc = pipe(filedes);
	pid = fork();
	if (pid == 0) {
	  time_t startRun;
	  ret=0;
	  close(filedes[0]);
	  logprintfl(EUCAINFO,"\tclient (%s) running instance: %s %s %s %s %d %s\n", res->ncURL, instId, amiId, mac, mac, vlan, keyName);
	  logprintfl(EUCAINFO,"\tasking for virtual hardware (mem/disk/cores): %d/%d/%d\n", ncvm.memorySize, ncvm.diskSize, ncvm.numberOfCores);
	  rc = 1;
	  startRun = time(NULL);
	  while(rc && ((time(NULL) - startRun) < config->wakeThresh)){
            int clientpid;

            // call StartNetwork client
            clientpid = fork();
	    if (!clientpid) {
	     ncs = ncStubCreate(res->ncURL, NULL, NULL);
	     if (config->use_wssec) {
	       rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	     }
	     rc = ncStartNetworkStub(ncs, ccMeta, NULL, 0, 0, vlan, NULL);
	     exit(0);
            } else {
	      rc = timewait(clientpid, &status, 30);
	    }

            // call RunInstances client
            ncs = ncStubCreate(res->ncURL, NULL, NULL);
	    if (config->use_wssec) {
	      rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	    }
	    rc = ncRunInstanceStub(ncs, ccMeta, instId, reservationId, &ncvm, amiId, amiURL, kernelId, kernelURL, ramdiskId, ramdiskURL, keyName, mac, mac, vlan, userData, launchIndex, netNames, netNamesLen, &outInst);
	  }
	  if (!rc) {
	    ret = 0;
	  } else {
	    ret = 1;
	  }
	  close(filedes[1]);	  
	  exit(ret);
	} else {
	  close(filedes[1]);
	  close(filedes[0]);
	  rc = 0;
	  logprintfl(EUCAINFO,"\tcall complete (pid/rc): %d/%d\n", pid, rc);
	}
	if (rc != 0) {
	  // problem
	  logprintfl(EUCAERROR, "tried to run the VM, but runInstance() failed; marking resource '%s' as down\n", res->ncURL);
	  res->state = RESDOWN;
	  i--;
	  // couldn't run this VM, remove networking information from system
	  sem_wait(vnetConfigLock);
	  vnetDisableHost(vnetconfig, mac, NULL, 0);
	  if (!strcmp(vnetconfig->mode, "MANAGED") || !strcmp(vnetconfig->mode, "MANAGED-NOVLAN")) {
	    vnetDelHost(vnetconfig, mac, NULL, vlan);
	  }
	  sem_post(vnetConfigLock);
	} else {
	  res->availMemory -= ccvm->mem;
	  res->availDisk -= ccvm->disk;
	  res->availCores -= ccvm->cores;

	  logprintfl(EUCADEBUG, "resource information after schedule/run: %d/%d, %d/%d, %d/%d\n", res->availMemory, res->maxMemory, res->availCores, res->maxCores, res->availDisk, res->maxDisk);

	  myInstance = &(retInsts[runCount]);
	  bzero(myInstance, sizeof(ccInstance));
	  
	  allocate_ccInstance(myInstance, instId, amiId, kernelId, ramdiskId, amiURL, kernelURL, ramdiskURL, ownerId, "Pending", time(NULL), reservationId, &(myInstance->ccnet), &(myInstance->ccvm), myInstance->ncHostIdx, keyName, myInstance->serviceTag, userData, launchIndex, myInstance->groupNames, myInstance->volumes, myInstance->volumesSize, myInstance->networkIndex);

	  // instance info that CC has
	  if (thenidx >= 0) {
	    myInstance->networkIndex = networkIndexList[thenidx];
	  }
	  myInstance->ts = time(NULL);
	  if (strcmp(pubip, "0.0.0.0")) {
	    strncpy(myInstance->ccnet.publicIp, pubip, 16);
	  }
	  if (strcmp(privip, "0.0.0.0")) {
	    strncpy(myInstance->ccnet.privateIp, privip, 16);
	  }
	  myInstance->ncHostIdx = resid;
	  if (ccvm) memcpy(&(myInstance->ccvm), ccvm, sizeof(virtualMachine));
	  if (&config->resourcePool[resid].ncURL != NULL) strncpy(myInstance->serviceTag, config->resourcePool[resid].ncURL, 64);
	  
	  strncpy(myInstance->ccnet.publicIp, pubip, 16);
	  strncpy(myInstance->ccnet.privateIp, privip, 16);
	  strncpy(myInstance->ccnet.publicMac, mac, 24);
	  strncpy(myInstance->ccnet.privateMac, mac, 24);
	  myInstance->ccnet.vlan = vlan;
	  
	  // start up DHCP
	  rc = vnetKickDHCP(vnetconfig);
	  if (rc) {
	    logprintfl(EUCAERROR, "cannot start DHCP daemon, for instance %s please check your network settings\n", myInstance->instanceId);
	  }
	  
	  // add the instance to the cache, and continue on
	  add_instanceCache(myInstance->instanceId, myInstance);

	  runCount++;
	}
      }
      sem_post(configLock);
    }
    
    if (instId) free(instId);
  }
  *outInstsLen = runCount;
  *outInsts = retInsts;
  
  logprintfl(EUCADEBUG,"RunInstances(): done\n");
  
  shawn();
  if (error) {
    return(1);
  }
  return(0);
}

int doGetConsoleOutput(ncMetadata *meta, char *instId, char **outConsoleOutput) {
  int i, j, rc, numInsts, start, stop, done, ret, rbytes;
  ccInstance *myInstance;
  ncStub *ncs;
  char *consoleOutput;
  time_t op_start, op_timer;

  i = j = numInsts = 0;
  op_start = time(NULL);
  op_timer = OP_TIMEOUT;

  consoleOutput = NULL;
  myInstance = NULL;
  
  *outConsoleOutput = NULL;

  rc = initialize();
  if (rc) {
    return(1);
  }

  logprintfl(EUCADEBUG,"GetConsoleOutput(): called\n");
  
  rc = find_instanceCacheId(instId, &myInstance);
  if (!rc) {
    // found the instance in the cache
    start = myInstance->ncHostIdx;
    stop = start+1;      
    free(myInstance);
  } else {
    start = 0;
    stop = config->numResources;
  }
  
  sem_wait(configLock);
  done=0;
  for (j=start; j<stop && !done; j++) {
    // read the instance ids
    logprintfl(EUCAINFO,"getConsoleOutput(): calling GetConsoleOutput for instance (%s) on (%s)\n", instId, config->resourcePool[j].hostname);
    if (1) {
      int pid, status, ret, len;
      int filedes[2];
      rc = pipe(filedes);
      pid = fork();
      if (pid == 0) {
	ret=0;
	close(filedes[0]);
	ncs = ncStubCreate(config->resourcePool[j].ncURL, NULL, NULL);
	if (config->use_wssec) {
	  rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	}

	rc = ncGetConsoleOutputStub(ncs, meta, instId, &consoleOutput);
	if (!rc && consoleOutput) {
	  len = strlen(consoleOutput) + 1;
	  rc = write(filedes[1], &len, sizeof(int));
	  rc = write(filedes[1], consoleOutput, sizeof(char) * len);
	  ret = 0;
	} else {
	  len = 0;
	  rc = write(filedes[1], &len, sizeof(int));
	  ret = 1;
	}
	close(filedes[1]);	  
	exit(ret);
      } else {
	close(filedes[1]);
	op_timer = OP_TIMEOUT - (time(NULL) - op_start);
	rbytes = timeread(filedes[0], &len, sizeof(int), minint(op_timer / ((stop-start) - (j - start)), OP_TIMEOUT_PERNODE));
	if (rbytes <= 0) {
	  // read went badly
	  kill(pid, SIGKILL);
	  wait(&status);
	  rc = -1;
	} else {
	  consoleOutput = malloc(sizeof(char) * len);
	  op_timer = OP_TIMEOUT - (time(NULL) - op_start);
	  rbytes = timeread(filedes[0], consoleOutput, len, minint(op_timer / ((stop-start) - (j-start)), OP_TIMEOUT_PERNODE));
	  if (rbytes <= 0) {
	    // read went badly
	    kill(pid, SIGKILL);
	    wait(&status);
	    rc = -1;
	  } else {
	    wait(&status);
	    rc = WEXITSTATUS(status);
	  }
	}
	close(filedes[0]);
	
	logprintfl(EUCAINFO,"\tcall complete (pid/rc): %d/%d\n", pid, rc);
	if (!rc) {
	  done++;
	} else {
	  if (consoleOutput) {
	    free(consoleOutput);
	    consoleOutput = NULL;
	  }
	}
      }
    }
  }
  sem_post(configLock);
  
  logprintfl(EUCADEBUG,"GetConsoleOutput(): done.\n");
  
  shawn();
  
  if (consoleOutput) {
    *outConsoleOutput = strdup(consoleOutput);
    ret = 0;
  } else {
    *outConsoleOutput = NULL;
    ret = 1;
  }
  if (consoleOutput) free(consoleOutput);
  return(ret);
}

int doRebootInstances(ncMetadata *meta, char **instIds, int instIdsLen) {
  int i, j, rc, numInsts, start, stop, done;
  char *instId;
  ccInstance *myInstance;
  ncStub *ncs;
  time_t op_start, op_timer;

  i = j = numInsts = 0;
  instId = NULL;
  myInstance = NULL;
  op_start = time(NULL);
  op_timer = OP_TIMEOUT;

  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG,"RebootInstances(): called\n");
  
  for (i=0; i<instIdsLen; i++) {
    instId = instIds[i];
    rc = find_instanceCacheId(instId, &myInstance);
    if (!rc) {
      // found the instance in the cache
      start = myInstance->ncHostIdx;
      stop = start+1;      
      free(myInstance);
    } else {
      start = 0;
      stop = config->numResources;
    }
    
    sem_wait(configLock);
    done=0;
    for (j=start; j<stop && !done; j++) {
      // read the instance ids
      logprintfl(EUCAINFO,"RebootInstances(): calling reboot instance (%s) on (%s)\n", instId, config->resourcePool[j].hostname);
      if (1) {
	int pid, status, ret;
	pid = fork();
	if (pid == 0) {
	  ret=0;
	  ncs = ncStubCreate(config->resourcePool[j].ncURL, NULL, NULL);
	  if (config->use_wssec) {
	    rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	  }
	  
	  rc = 0;
	  rc = ncRebootInstanceStub(ncs, meta, instId);
	  
	  if (!rc) {
	    ret = 0;
	  } else {
	    ret = 1;
	  }
	  exit(ret);
	} else {
	  op_timer = OP_TIMEOUT - (time(NULL) - op_start);
	  rc = timewait(pid, &status, minint(op_timer / ((stop-start) - (j-start)), OP_TIMEOUT_PERNODE));
	  rc = WEXITSTATUS(status);
	  logprintfl(EUCAINFO,"\tcall complete (pid/rc): %d/%d\n", pid, rc);
	}
      }
      sem_post(configLock);
      
      if (!rc) {
	done++;
      }
    }
  }
  
  logprintfl(EUCADEBUG,"RebootInstances(): done.\n");

  shawn();

  return(0);
}

int doTerminateInstances(ncMetadata *ccMeta, char **instIds, int instIdsLen, int **outStatus) {
  int i, j, shutdownState, previousState, rc, start, stop;
  char *instId;
  ccInstance *myInstance;
  ncStub *ncs;
  time_t op_start, op_timer;

  i = j = 0;
  instId = NULL;
  myInstance = NULL;
  op_start = time(NULL);
  op_timer = OP_TIMEOUT;

  rc = initialize();
  if (rc) {
    return(1);
  }
  logprintfl(EUCADEBUG,"TerminateInstances(): called\n");
  
  for (i=0; i<instIdsLen; i++) {
    instId = instIds[i];
    rc = find_instanceCacheId(instId, &myInstance);
    if (!rc) {
      // found the instance in the cache
      start = myInstance->ncHostIdx;
      stop = start+1;
      
      // remove private network info from system
      sem_wait(vnetConfigLock);
      
      vnetDisableHost(vnetconfig, myInstance->ccnet.privateMac, NULL, 0);
      if (!strcmp(vnetconfig->mode, "MANAGED") || !strcmp(vnetconfig->mode, "MANAGED-NOVLAN")) {
	vnetDelHost(vnetconfig, myInstance->ccnet.privateMac, NULL, myInstance->ccnet.vlan);
      }
      
      sem_post(vnetConfigLock);
      
      free(myInstance);
    } else {
      start = 0;
      stop = config->numResources;
    }
    
    sem_wait(configLock);
    for (j=start; j<stop; j++) {
      // read the instance ids
      logprintfl(EUCAINFO,"TerminateInstances(): calling terminate instance (%s) on (%s)\n", instId, config->resourcePool[j].hostname);
      if (config->resourcePool[j].state == RESUP) {
	int pid, status, ret;
	int filedes[2];
	rc = pipe(filedes);
	pid = fork();
	if (pid == 0) {
	  ret=0;
	  close(filedes[0]);
	  ncs = ncStubCreate(config->resourcePool[j].ncURL, NULL, NULL);
	  if (config->use_wssec) {
	    rc = InitWSSEC(ncs->env, ncs->stub, config->policyFile);
	  }
	  rc = ncTerminateInstanceStub(ncs, ccMeta, instId, &shutdownState, &previousState);
	  
	  if (!rc) {
	    ret = 0;
	  } else {
	    ret = 1;
	  }
	  close(filedes[1]);	  
	  exit(ret);
	} else {
	  close(filedes[1]);
	  close(filedes[0]);
	  
	  op_timer = OP_TIMEOUT - (time(NULL) - op_start);
	  rc = timewait(pid, &status, minint(op_timer / ((stop-start) - (j - start)), OP_TIMEOUT_PERNODE));
	  rc = WEXITSTATUS(status);
	  logprintfl(EUCADEBUG,"\tcall complete (pid/rc): %d/%d\n", pid, rc);
	}

	if (!rc) {
	  del_instanceCacheId(instId);
	  (*outStatus)[i] = 1;
	  logprintfl(EUCAWARN, "failed to terminate '%s': instance may not exist any longer\n", instId);
	} else {
	  (*outStatus)[i] = 0;
	}
      }
    }
    sem_post(configLock);
  }
  
  logprintfl(EUCADEBUG,"TerminateInstances(): done.\n");
  
  shawn();

  return(0);
}

int setup_shared_buffer(void **buf, char *bufname, size_t bytes, sem_t **lock, char *lockname, int mode) {
  int shd, rc, ret;
  
  // create a lock and grab it
  *lock = sem_open(lockname, O_CREAT, 0644, 1);    
  sem_wait(*lock);
  ret=0;

  if (mode == SHARED_MEM) {
    // set up shared memory segment for config
    shd = shm_open(bufname, O_CREAT | O_RDWR | O_EXCL, 0644);
    if (shd >= 0) {
      // if this is the first process to create the config, init to 0
      rc = ftruncate(shd, bytes);
    } else {
      shd = shm_open(bufname, O_CREAT | O_RDWR, 0644);
    }
    if (shd < 0) {
      fprintf(stderr, "cannot initialize shared memory segment\n");
      sem_post(*lock);
      sem_close(*lock);
      return(1);
    }
    *buf = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, shd, 0);
  } else if (mode == SHARED_FILE) {
    char *tmpstr, path[1024];
    struct stat mystat;
    int fd;
    
    tmpstr = getenv(EUCALYPTUS_ENV_VAR_NAME);
    if (!tmpstr) {
      snprintf(path, 1024, "/var/lib/eucalyptus/CC/%s", bufname);
    } else {
      snprintf(path, 1024, "%s/var/lib/eucalyptus/CC/%s", tmpstr, bufname);
    }
    fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd<0) {
      fprintf(stderr, "ERROR: cannot open/create '%s' to set up mmapped buffer\n", path);
      ret = 1;
    } else {
      mystat.st_size = 0;
      rc = fstat(fd, &mystat);
      // this is the check to make sure we're dealing with a valid prior config
      if (mystat.st_size != bytes) {
	rc = ftruncate(fd, bytes);
      }
      *buf = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
      if (*buf == NULL) {
	fprintf(stderr, "ERROR: cannot mmap fd\n");
	ret = 1;
      }
      close(fd);
    }
  }
  sem_post(*lock);
  return(ret);
}

int sem_timepost(sem_t *sem) {
  int rc;
  rc = sem_post(sem);
  if (rc == 0) {
  }
  return(rc);
}

int sem_timewait(sem_t *sem, time_t seconds) {
  int rc;
  struct timespec to;

  to.tv_sec = time(NULL) + seconds + 1;
  to.tv_nsec = 0;
  
  rc = sem_timedwait(sem, &to);
  if (rc < 0) {
    perror("SEM");
    logprintfl(EUCAERROR, "timeout waiting for semaphore\n");
  } else {
  }
  return(rc);
}

int initialize(void) {
  int rc, ret;

  ret=0;
  rc = init_thread();
  if (rc) {
    ret=1;
    logprintfl(EUCAERROR, "cannot initialize thread\n");
  }

  rc = init_localstate();
  if (rc) {
    ret = 1;
    logprintfl(EUCAERROR, "cannot initialize local state\n");
  }

  rc = init_config();
  if (rc) {
    ret=1;
    logprintfl(EUCAERROR, "cannot initialize from configuration file\n");
  }
  
  rc = vnetInitTunnels(vnetconfig);
  if (rc) {
    logprintfl(EUCAERROR, "cannot initialize tunnels\n");
  }

  rc = init_pthreads();
  if (rc) {
    logprintfl(EUCAERROR, "cannot initialize background threads\n");
    ret = 1;
  }

  if (!ret) {
    // initialization went well, this thread is now initialized
    init=1;
  }
  
  return(ret);
}

void *monitor_thread(void *in) {
  int rc;
  ncMetadata ccMeta;
  ccMeta.correlationId = strdup("monitor");
  ccMeta.userId = strdup("eucalyptus");

  while(1) {
    // set up default signal handler for this child process (for SIGTERM)
    struct sigaction newsigact;
    newsigact.sa_handler = SIG_DFL;
    newsigact.sa_flags = 0;
    sigemptyset(&newsigact.sa_mask);
    sigprocmask(SIG_SETMASK, &newsigact.sa_mask, NULL);
    sigaction(SIGTERM, &newsigact, NULL);

    logprintfl(EUCADEBUG, "monitor running\n");

    rc = refresh_resources(&ccMeta, 60, 1);
    if (rc) {
      logprintfl(EUCAWARN, "call to refresh_resources() failed in monitor thread\n");
    }
    rc = refresh_instances(&ccMeta, 60, 1);
    if (rc) {
      logprintfl(EUCAWARN, "call to refresh_instances() failed in monitor thread\n");
    }
    logprintfl(EUCADEBUG, "monitor done\n");
    sleep(6);
  }
  return(NULL);
}

int init_pthreads() {
  // start any background threads
  sem_wait(configLock);
  if (config->threads[MONITOR] == 0) {
    int pid;
    pid = fork();
    if (!pid) {
      // set up default signal handler for this child process (for SIGTERM)
      struct sigaction newsigact;
      newsigact.sa_handler = SIG_DFL;
      newsigact.sa_flags = 0;
      sigemptyset(&newsigact.sa_mask);
      sigprocmask(SIG_SETMASK, &newsigact.sa_mask, NULL);
      sigaction(SIGTERM, &newsigact, NULL);

      monitor_thread(NULL);
      exit(0);
    } else {
      config->threads[MONITOR] = pid;
    }
  }

  sem_post(configLock);

  return(0);
}

int init_localstate(void) {
  int rc, loglevel, ret;
  char *tmpstr=NULL, logFile[1024], configFile[1024], home[1024], vfile[1024];

  ret=0;
  if (init) {
  } else {
    // thread is not initialized, run first time local state setup
    bzero(logFile, 1024);
    bzero(home, 1024);
    bzero(configFile, 1024);
    
    tmpstr = getenv(EUCALYPTUS_ENV_VAR_NAME);
    if (!tmpstr) {
      snprintf(home, 1024, "/");
    } else {
      snprintf(home, 1024, "%s", tmpstr);
    }
    
    snprintf(configFile, 1024, EUCALYPTUS_CONF_LOCATION, home);
    snprintf(logFile, 1024, "%s/var/log/eucalyptus/cc.log", home);  
    
    rc = get_conf_var(configFile, "LOGLEVEL", &tmpstr);
    if (rc != 1) {
      loglevel = EUCADEBUG;
    } else {
      if (!strcmp(tmpstr,"DEBUG")) {loglevel=EUCADEBUG;}
      else if (!strcmp(tmpstr,"INFO")) {loglevel=EUCAINFO;}
      else if (!strcmp(tmpstr,"WARN")) {loglevel=EUCAWARN;}
      else if (!strcmp(tmpstr,"ERROR")) {loglevel=EUCAERROR;}
      else if (!strcmp(tmpstr,"FATAL")) {loglevel=EUCAFATAL;}
      else {loglevel=EUCADEBUG;}
    }
    if (tmpstr) free(tmpstr);
    // set up logfile
    logfile(logFile, loglevel);
   
  }

  return(ret);
}

int init_thread(void) {
  int rc;
  
  if (init) {
    // thread has already been initialized
  } else {
    // this thread has not been initialized, set up shared memory segments
    srand(time(NULL));

    initLock = sem_open("/eucalyptusCCinitLock", O_CREAT, 0644, 1);    
    sem_wait(initLock);
    
    if (config == NULL) {
      rc = setup_shared_buffer((void **)&config, "/eucalyptusCCConfig", sizeof(ccConfig), &configLock, "/eucalyptusCCConfigLock", SHARED_FILE);
      if (rc != 0) {
	fprintf(stderr, "Cannot set up shared memory region for ccConfig, exiting...\n");
	sem_post(initLock);
	exit(1);
      }
    }
    
    if (instanceCache == NULL) {
      rc = setup_shared_buffer((void **)&instanceCache, "/eucalyptusCCInstanceCache", sizeof(ccInstanceCache), &instanceCacheLock, "/eucalyptusCCInstanceCacheLock", SHARED_FILE);
      if (rc != 0) {
	fprintf(stderr, "Cannot set up shared memory region for ccInstanceCache, exiting...\n");
	sem_post(initLock);
	exit(1);
      }
    }
    
    if (vnetconfig == NULL) {
      rc = setup_shared_buffer((void **)&vnetconfig, "/eucalyptusCCVNETConfig", sizeof(vnetConfig), &vnetConfigLock, "/eucalyptusCCVNETConfigLock", SHARED_FILE);
      if (rc != 0) {
	fprintf(stderr, "Cannot set up shared memory region for ccVNETConfig, exiting...\n");
	sem_post(initLock);
	exit(1);
      }
    }
    sem_post(initLock);
  }
  return(0);
}

int init_config(void) {
  resource *res=NULL;
  char *tmpstr=NULL;
  int rc, numHosts, use_wssec, schedPolicy, idleThresh, wakeThresh, ret, i;
  
  char configFile[1024], netPath[1024], eucahome[1024], policyFile[1024], home[1024];
  
  time_t configMtime;
  struct stat statbuf;
  
  // read in base config information
  tmpstr = getenv(EUCALYPTUS_ENV_VAR_NAME);
  if (!tmpstr) {
    snprintf(home, 1024, "/");
  } else {
    snprintf(home, 1024, "%s", tmpstr);
  }
  
  bzero(configFile, 1024);
  bzero(netPath, 1024);
  bzero(policyFile, 1024);
  
  snprintf(configFile, 1024, EUCALYPTUS_CONF_LOCATION, home);
  snprintf(netPath, 1024, CC_NET_PATH_DEFAULT, home);
  snprintf(policyFile, 1024, "%s/var/lib/eucalyptus/keys/nc-client-policy.xml", home);
  snprintf(eucahome, 1024, "%s/", home);

  // stat the config file, update modification time
  rc = stat(configFile, &statbuf);
  if (rc) {
    logprintfl(EUCAERROR, "cannot stat configfile '%s'\n", configFile);
    return(1);
  } 
  configMtime = statbuf.st_mtime;
  
  if (init) {
    // this means that this thread has already been initialized
    ret = 0;

    // check to see if the configfile has changed
    if (config->configMtime != configMtime) {
      // something has changed
      config->configMtime = configMtime;
      
      logprintfl(EUCAINFO, "config file has been modified, refreshing node list\n");
      res = NULL;
      rc = refreshNodes(config, configFile, &res, &numHosts);
      if (rc) {
	logprintfl(EUCAERROR, "cannot read list of nodes, check your config file\n");
	sem_wait(configLock);
	config->numResources = 0;
	bzero(config->resourcePool, sizeof(resource) * MAXNODES);
	sem_post(configLock);
	ret = 1;
      } else {
	sem_wait(configLock);
	config->numResources = numHosts;
	memcpy(config->resourcePool, res, sizeof(resource) * numHosts);
	free(res);
	{
	  ncMetadata ccMeta;
	  ccMeta.correlationId = strdup("monitor");
	  ccMeta.userId = strdup("eucalyptus");
	  refresh_resources(&ccMeta, 60, 0);
	}
	sem_post(configLock);
      }
    }
    return(ret);
  }
  
  if (config->initialized) {
    // some other thread has already initialized the configuration
    logprintfl(EUCAINFO, "init(): another thread has already set up config\n");
    logprintfl(EUCADEBUG, "printing instance cache in init_config()\n");
    print_instanceCache();
    rc = restoreNetworkState();
    if (rc) {
      // failed to restore network state, continue 
      logprintfl(EUCAWARN, "restoreNetworkState returned false (may be already restored)\n");
    }
    return(0);
  }
  
  logprintfl(EUCADEBUG,"init_config(): initializing CC configuration\n");  
  
  // DHCP configuration section
  {
    char *daemon=NULL,
      *dhcpuser=NULL,
      *numaddrs=NULL,
      *pubmode=NULL,
      *pubmacmap=NULL,
      *pubips=NULL,
      *pubInterface=NULL,
      *privInterface=NULL,
      *pubSubnet=NULL,
      *pubSubnetMask=NULL,
      *pubBroadcastAddress=NULL,
      *pubRouter=NULL,
      *pubDNS=NULL,
      *localIp=NULL,
      *cloudIp=NULL;
    uint32_t *ips, *nms;
    int initFail=0, len;
    
    // DHCP Daemon Configuration Params
    daemon = getConfString(configFile, "VNET_DHCPDAEMON");
    if (!daemon) {
      logprintfl(EUCAWARN,"no VNET_DHCPDAEMON defined in config, using default\n");
    }
    
    dhcpuser = getConfString(configFile, "VNET_DHCPUSER");
    if (!dhcpuser) {
      dhcpuser = strdup("root");
      if (!dhcpuser)
         logprintfl(EUCAWARN,"Out of memory\n");
    }
    
    pubmode = getConfString(configFile, "VNET_MODE");
    if (!pubmode) {
      logprintfl(EUCAWARN,"VNET_MODE is not defined, defaulting to 'SYSTEM'\n");
      pubmode = strdup("SYSTEM");
      if (!pubmode)
         logprintfl(EUCAWARN,"Out of memory\n");
    }
    
    {
      int usednew=0;
      
      pubInterface = getConfString(configFile, "VNET_PUBINTERFACE");
      if (!pubInterface) {
	logprintfl(EUCAWARN,"VNET_PUBINTERFACE is not defined, defaulting to 'eth0'\n");
	pubInterface = strdup("eth0");
      } else {
	usednew=1;
      }
      
      privInterface = NULL;
      privInterface = getConfString(configFile, "VNET_PRIVINTERFACE");
      if (!privInterface) {
	logprintfl(EUCAWARN,"VNET_PRIVINTERFACE is not defined, defaulting to 'eth0'\n");
	privInterface = strdup("eth0");
	usednew = 0;
      }
      
      if (!usednew) {
	tmpstr = NULL;
	tmpstr = getConfString(configFile, "VNET_INTERFACE");
	if (tmpstr) {
	  logprintfl(EUCAWARN, "VNET_INTERFACE is deprecated, please use VNET_PUBINTERFACE and VNET_PRIVINTERFACE instead.  Will set both to value of VNET_INTERFACE (%s) for now.\n", tmpstr);
	  if (pubInterface) free(pubInterface);
	  pubInterface = strdup(tmpstr);
	  if (privInterface) free(privInterface);
	  privInterface = strdup(tmpstr);
	}
	if (tmpstr) free(tmpstr);
      }
    }

    if (pubmode && !strcmp(pubmode, "STATIC")) {
      pubSubnet = getConfString(configFile, "VNET_SUBNET");
      pubSubnetMask = getConfString(configFile, "VNET_NETMASK");
      pubBroadcastAddress = getConfString(configFile, "VNET_BROADCAST");
      pubRouter = getConfString(configFile, "VNET_ROUTER");
      pubDNS = getConfString(configFile, "VNET_DNS");
      pubmacmap = getConfString(configFile, "VNET_MACMAP");

      if (!pubSubnet || !pubSubnetMask || !pubBroadcastAddress || !pubRouter || !pubDNS || !pubmacmap) {
	logprintfl(EUCAFATAL,"in 'STATIC' network mode, you must specify values for 'VNET_SUBNET, VNET_NETMASK, VNET_BROADCAST, VNET_ROUTER, VNET_DNS, and VNET_MACMAP'\n");
	initFail = 1;
      }
    } else if (pubmode && (!strcmp(pubmode, "MANAGED") || !strcmp(pubmode, "MANAGED-NOVLAN"))) {
      numaddrs = getConfString(configFile, "VNET_ADDRSPERNET");
      pubSubnet = getConfString(configFile, "VNET_SUBNET");
      pubSubnetMask = getConfString(configFile, "VNET_NETMASK");
      pubDNS = getConfString(configFile, "VNET_DNS");
      pubips = getConfString(configFile, "VNET_PUBLICIPS");
      localIp = getConfString(configFile, "VNET_LOCALIP");
      if (!localIp) {
	logprintfl(EUCAWARN, "VNET_LOCALIP not defined, will attempt to auto-discover (consider setting this explicitly if tunnelling does not function properly.)\n");
      }
      cloudIp = getConfString(configFile, "VNET_CLOUDIP");

      if (!pubSubnet || !pubSubnetMask || !pubDNS || !numaddrs) {
	logprintfl(EUCAFATAL,"in 'MANAGED' or 'MANAGED-NOVLAN' network mode, you must specify values for 'VNET_SUBNET, VNET_NETMASK, VNET_ADDRSPERNET, and VNET_DNS'\n");
	initFail = 1;
      }
    }
    
    if (initFail) {
      logprintfl(EUCAFATAL, "bad network parameters, must fix before system will work\n");
      if (cloudIp) free(cloudIp);
      if (pubSubnet) free(pubSubnet);
      if (pubSubnetMask) free(pubSubnetMask);
      if (pubBroadcastAddress) free(pubBroadcastAddress);
      if (pubRouter) free(pubRouter);
      if (pubDNS) free(pubDNS);
      if (pubmacmap) free(pubmacmap);
      if (numaddrs) free(numaddrs);
      if (pubips) free(pubips);
      if (localIp) free(localIp);
      if (pubInterface) free(pubInterface);
      if (privInterface) free(privInterface);
      if (dhcpuser) free(dhcpuser);
      if (daemon) free(daemon);
      if (pubmode) free(pubmode);
      return(1);
    }
    
    sem_wait(vnetConfigLock);
    
    vnetInit(vnetconfig, pubmode, eucahome, netPath, CLC, pubInterface, privInterface, numaddrs, pubSubnet, pubSubnetMask, pubBroadcastAddress, pubDNS, pubRouter, daemon, dhcpuser, NULL, localIp, cloudIp);
    if (cloudIp) free(cloudIp);
    if (pubSubnet) free(pubSubnet);
    if (pubSubnetMask) free(pubSubnetMask);
    if (pubBroadcastAddress) free(pubBroadcastAddress);
    if (pubDNS) free(pubDNS);
    if (pubRouter) free(pubRouter);
    if (numaddrs) free(numaddrs);
    if (pubmode) free(pubmode);
    if (dhcpuser) free(dhcpuser);
    if (daemon) free(daemon);
    if (privInterface) free(privInterface);
    if (pubInterface) free(pubInterface);
    
    vnetAddDev(vnetconfig, vnetconfig->privInterface);

    if (pubmacmap) {
      char *mac=NULL, *ip=NULL, *ptra=NULL, *toka=NULL, *ptrb=NULL;
      toka = strtok_r(pubmacmap, " ", &ptra);
      while(toka) {
	mac = ip = NULL;
	mac = strtok_r(toka, "=", &ptrb);
	ip = strtok_r(NULL, "=", &ptrb);
	if (mac && ip) {
	  vnetAddHost(vnetconfig, mac, ip, 0, -1);
	}
	toka = strtok_r(NULL, " ", &ptra);
      }
      vnetKickDHCP(vnetconfig);
      free(pubmacmap);
    } else if (pubips) {
      char *ip, *ptra, *toka;
      toka = strtok_r(pubips, " ", &ptra);
      while(toka) {
	ip = toka;
	if (ip) {
	  rc = vnetAddPublicIP(vnetconfig, ip);
	  if (rc) {
	    logprintfl(EUCAERROR, "could not add public IP '%s'\n", ip);
	  }
	}
	toka = strtok_r(NULL, " ", &ptra);
      }

      // detect and populate ips
      if (vnetCountLocalIP(vnetconfig) <= 0) {
	ips = nms = NULL;
	rc = getdevinfo("all", &ips, &nms, &len);
	if (!rc) {
	  for (i=0; i<len; i++) {
	    char *theip=NULL;
	    theip = hex2dot(ips[i]);
	    if (vnetCheckPublicIP(vnetconfig, theip)) {
	      vnetAddLocalIP(vnetconfig, ips[i]);
	    }
	    if (theip) free(theip);
	  }
	}
	if (ips) free(ips);
	if (nms) free(nms);
      }
      free(pubips);
    }
    
    sem_post(vnetConfigLock);
  }
  
  rc = get_conf_var(configFile, "SCHEDPOLICY", &tmpstr);
  if (rc != 1) {
    // error
    logprintfl(EUCAWARN,"parsing config file (%s) for SCHEDPOLICY, defaulting to GREEDY\n", configFile);
    schedPolicy = SCHEDGREEDY;
    tmpstr = NULL;
  } else {
    if (!strcmp(tmpstr, "GREEDY")) schedPolicy = SCHEDGREEDY;
    else if (!strcmp(tmpstr, "ROUNDROBIN")) schedPolicy = SCHEDROUNDROBIN;
    else if (!strcmp(tmpstr, "POWERSAVE")) schedPolicy = SCHEDPOWERSAVE;
    else schedPolicy = SCHEDGREEDY;
  }
  if (tmpstr) free(tmpstr);

  // powersave options
  rc = get_conf_var(configFile, "POWER_IDLETHRESH", &tmpstr);
  if (rc != 1) {
    logprintfl(EUCAWARN,"parsing config file (%s) for POWER_IDLETHRESH, defaulting to 300 seconds\n", configFile);
    idleThresh = 300;
    tmpstr = NULL;
  } else {
    idleThresh = atoi(tmpstr);
    if (idleThresh < 300) {
      logprintfl(EUCAWARN, "POWER_IDLETHRESH set too low (%d seconds), resetting to minimum (300 seconds)\n", idleThresh);
      idleThresh = 300;
    }
  }
  if (tmpstr) free(tmpstr);

  rc = get_conf_var(configFile, "POWER_WAKETHRESH", &tmpstr);
  if (rc != 1) {
    logprintfl(EUCAWARN,"parsing config file (%s) for POWER_WAKETHRESH, defaulting to 300 seconds\n", configFile);
    wakeThresh = 300;
    tmpstr = NULL;
  } else {
    wakeThresh = atoi(tmpstr);
    if (wakeThresh < 300) {
      logprintfl(EUCAWARN, "POWER_WAKETHRESH set too low (%d seconds), resetting to minimum (300 seconds)\n", wakeThresh);
      wakeThresh = 300;
    }
  }
  if (tmpstr) free(tmpstr);

  // WS-Security
  use_wssec = 0;
  tmpstr = getConfString(configFile, "ENABLE_WS_SECURITY");
  if (!tmpstr) {
    // error
    logprintfl(EUCAFATAL,"parsing config file (%s) for ENABLE_WS_SECURITY\n", configFile);
    return(1);
  } else {
    if (!strcmp(tmpstr, "Y")) {
      use_wssec = 1;
    }
  }
  if (tmpstr) free(tmpstr);

  res = NULL;
  rc = refreshNodes(config, configFile, &res, &numHosts);
  if (rc) {
    logprintfl(EUCAERROR, "cannot read list of nodes, check your config file\n");
    return(1);
  }
  
  sem_wait(configLock);
  // set up the current config   
  strncpy(config->eucahome, eucahome, 1024);
  strncpy(config->policyFile, policyFile, 1024);
  config->use_wssec = use_wssec;
  config->schedPolicy = schedPolicy;
  config->idleThresh = idleThresh;
  config->wakeThresh = wakeThresh;
  config->numResources = numHosts;
  if (numHosts) {
    memcpy(config->resourcePool, res, sizeof(resource) * numHosts);
  }
  if (res) free(res);
  config->lastResourceUpdate = 0;
  //  config->instanceCacheUpdate = time(NULL);
  config->configMtime = configMtime;
  config->initialized = 1;
  sem_post(configLock);
  
  logprintfl(EUCADEBUG,"init_config(): done\n");
  
  return(0);
}

int maintainNetworkState() {
  int rc, i, ret=0;
  time_t startTime, startTimeA;

  if (!strcmp(vnetconfig->mode, "MANAGED") || !strcmp(vnetconfig->mode, "MANAGED-NOVLAN")) {
    sem_wait(vnetConfigLock);
    
    rc = vnetSetupTunnels(vnetconfig);

    if (rc) {
      logprintfl(EUCAERROR, "failed to setup tunnels during maintainNetworkState()\n");
      ret = 1;
    }
    
    for (i=2; i<NUMBER_OF_VLANS; i++) {
      if (vnetconfig->networks[i].active) {
	char brname[32];
	if (!strcmp(vnetconfig->mode, "MANAGED")) {
	  snprintf(brname, 32, "eucabr%d", i);
	} else {
	  snprintf(brname, 32, "%s", vnetconfig->privInterface);
	}
	startTime=time(NULL);
	rc = vnetAttachTunnels(vnetconfig, i, brname);
	if (rc) {
	  logprintfl(EUCADEBUG, "failed to attach tunnels for vlan %d during maintainNetworkState()\n", i);
	  ret = 1;
	}
      }
    }
    sem_post(vnetConfigLock);
  }
  
  return(ret);
}

int restoreNetworkState() {
  int rc, ret=0, i;
  char cmd[1024];

  logprintfl(EUCAINFO, "restoring network state\n");
  sem_wait(vnetConfigLock);

  // restore iptables state                                                                                    
  logprintfl(EUCAINFO, "restarting iptables\n");
  rc = vnetRestoreTablesFromMemory(vnetconfig);
  if (rc) {
    logprintfl(EUCAERROR, "cannot restore iptables state\n");
    ret = 1;
  }
  
  // restore ip addresses                                                                                      
  logprintfl(EUCAINFO, "restarting ips\n");
  if (!strcmp(vnetconfig->mode, "MANAGED") || !strcmp(vnetconfig->mode, "MANAGED-NOVLAN")) {
    snprintf(cmd, 255, "%s/usr/lib/eucalyptus/euca_rootwrap ip addr add 169.254.169.254/32 scope link dev %s", config->eucahome, vnetconfig->privInterface);
    logprintfl(EUCAINFO,"running cmd %s\n", cmd);
    rc = system(cmd);
    if (rc) {
      logprintfl(EUCAWARN, "cannot add ip 169.254.169.254\n");
    }
  }
  for (i=1; i<NUMBER_OF_PUBLIC_IPS; i++) {
    if (vnetconfig->publicips[i].allocated) {
      char *tmp;

      tmp = hex2dot(vnetconfig->publicips[i].ip);
      snprintf(cmd, 255, "%s/usr/lib/eucalyptus/euca_rootwrap ip addr add %s/32 dev %s", config->eucahome, tmp, vnetconfig->pubInterface);
      logprintfl(EUCAINFO,"running cmd %s\n", cmd);
      rc = system(cmd);
      if (rc) {
        logprintfl(EUCAWARN, "cannot add ip %s\n", tmp);
      }
      free(tmp);
    }
  }

  // re-create all active networks
  logprintfl(EUCAINFO, "restarting networks\n");
  for (i=2; i<NUMBER_OF_VLANS; i++) {
    if (vnetconfig->networks[i].active) {
      char *brname=NULL;
      logprintfl(EUCADEBUG, "found active network: %d\n", i);
      rc = vnetStartNetwork(vnetconfig, i, vnetconfig->users[i].userName, vnetconfig->users[i].netName, &brname);
      if (rc) {
        logprintfl(EUCADEBUG, "failed to reactivate network: %d", i);
      }
      if (brname) free(brname);
    }
  }
  // get DHCPD back up and running
  logprintfl(EUCAINFO, "restarting DHCPD\n");
  rc = vnetKickDHCP(vnetconfig);
  if (rc) {
    logprintfl(EUCAERROR, "cannot start DHCP daemon, please check your network settings\n");
    ret = 1;
  }
  sem_post(vnetConfigLock);
  logprintfl(EUCADEBUG, "done restoring network state\n");

  return(ret);
}

int refreshNodes(ccConfig *config, char *configFile, resource **res, int *numHosts) {
  int rc, i;
  char *tmpstr, *ipbuf;
  char *ncservice;
  int ncport;
  char **hosts;

  *numHosts = 0;
  *res = NULL;

  rc = get_conf_var(configFile, CONFIG_NC_SERVICE, &tmpstr);
  if (rc != 1) {
    // error
    logprintfl(EUCAFATAL,"parsing config file (%s) for NC_SERVICE\n", configFile);
    return(1);
  } else {
    if(tmpstr)
      ncservice = strdup(tmpstr);
  }
  if (tmpstr) free(tmpstr);

  rc = get_conf_var(configFile, CONFIG_NC_PORT, &tmpstr);
  if (rc != 1) {
    // error
    free(ncservice);
    logprintfl(EUCAFATAL,"parsing config file (%s) for NC_PORT\n", configFile);
    return(1);
  } else {
    if(tmpstr)
      ncport = atoi(tmpstr);
  }
  if (tmpstr) free(tmpstr);

  rc = get_conf_var(configFile, CONFIG_NODES, &tmpstr);
  if (rc != 1) {
    // error
    free(ncservice);
    logprintfl(EUCAWARN,"NODES parameter is missing from (%s)\n", configFile);
    return(0);
  } else {
    hosts = from_var_to_char_list(tmpstr);
    if (hosts == NULL) {
      free(ncservice);
      logprintfl(EUCAWARN, "NODES list is empty in configfile (%s)\n", configFile);
      if (tmpstr) free(tmpstr);
      return(0);
    }

    *numHosts = 0;
    i = 0;
    while(hosts[i] != NULL) {
      (*numHosts)++;
      *res = realloc(*res, sizeof(resource) * *numHosts);
      bzero(&((*res)[*numHosts-1]), sizeof(resource));
      snprintf((*res)[*numHosts-1].hostname, 128, "%s", hosts[i]);

      ipbuf = host2ip(hosts[i]);
      if (ipbuf) {
	snprintf((*res)[*numHosts-1].ip, 24, "%s", ipbuf);
      }
      if (ipbuf) free(ipbuf);

      (*res)[*numHosts-1].ncPort = ncport;
      snprintf((*res)[*numHosts-1].ncService, 128, "%s", ncservice);
      snprintf((*res)[*numHosts-1].ncURL, 128, "http://%s:%d/%s", hosts[i], ncport, ncservice);	
      (*res)[*numHosts-1].state = RESDOWN;
      (*res)[*numHosts-1].lastState = RESDOWN;
      free(hosts[i]);
      i++;
    }
  }
  free(ncservice);
  if (hosts) free(hosts);
  if (tmpstr) free(tmpstr);
  return(0);
}

void shawn() {
  int p=1, status, rc;

  // clean up any orphaned child processes
  while(p > 0) {
    p = waitpid(-1, &status, WNOHANG);
  }
  
  rc = maintainNetworkState();
  if (rc) {
    logprintfl(EUCAERROR, "network state maintainance failed\n");
  }
}

int timeread(int fd, void *buf, size_t bytes, int timeout) {
  int rc;
  fd_set rfds;
  struct timeval tv;

  if (timeout <= 0) timeout = 1;

  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);
  
  tv.tv_sec = timeout;
  tv.tv_usec = 0;
  
  rc = select(fd+1, &rfds, NULL, NULL, &tv);
  if (rc <= 0) {
    // timeout
    logprintfl(EUCAERROR, "select() timed out for read: timeout=%d\n", timeout);
    return(-1);
  }
  rc = read(fd, buf, bytes);
  return(rc);
}

int allocate_ccInstance(ccInstance *out, char *id, char *amiId, char *kernelId, char *ramdiskId, char *amiURL, char *kernelURL, char *ramdiskURL, char *ownerId, char *state, time_t ts, char *reservationId, netConfig *ccnet, virtualMachine *ccvm, int ncHostIdx, char *keyName, char *serviceTag, char *userData, char *launchIndex, char groupNames[][32], ncVolume *volumes, int volumesSize, int networkIndex) {
  if (out != NULL) {
    bzero(out, sizeof(ccInstance));
    if (id) strncpy(out->instanceId, id, 16);
    if (amiId) strncpy(out->amiId, amiId, 16);
    if (kernelId) strncpy(out->kernelId, kernelId, 16);
    if (ramdiskId) strncpy(out->ramdiskId, ramdiskId, 16);
    
    if (amiURL) strncpy(out->amiURL, amiURL, 64);
    if (kernelURL) strncpy(out->kernelURL, kernelURL, 64);
    if (ramdiskURL) strncpy(out->ramdiskURL, ramdiskURL, 64);
    
    if (state) strncpy(out->state, state, 16);
    if (ownerId) strncpy(out->ownerId, ownerId, 16);
    if (reservationId) strncpy(out->reservationId, reservationId, 16);
    if (keyName) strncpy(out->keyName, keyName, 1024);
    out->ts = ts;
    out->ncHostIdx = ncHostIdx;
    if (serviceTag) strncpy(out->serviceTag, serviceTag, 64);
    if (userData) strncpy(out->userData, userData, 64);
    if (launchIndex) strncpy(out->launchIndex, launchIndex, 64);
    if (groupNames) {
      int i;
      for (i=0; i<64; i++) {
	if (groupNames[i]) {
	  strncpy(out->groupNames[i], groupNames[i], 32);
	}
      }
    }

    if (volumes) {
      memcpy(out->volumes, volumes, sizeof(ncVolume) * EUCA_MAX_VOLUMES);
    }
    out->volumesSize = volumesSize;
    if (networkIndex) out->networkIndex = networkIndex;

    if (ccnet) allocate_netConfig(&(out->ccnet), ccnet->privateMac, ccnet->publicMac, ccnet->privateIp, ccnet->publicIp, ccnet->vlan);
    if (ccvm) allocate_virtualMachine(&(out->ccvm), ccvm->mem, ccvm->disk, ccvm->cores, ccvm->name);    
  }
  return(0);
}

int allocate_netConfig(netConfig *out, char *pvMac, char *pbMac, char *pvIp, char *pbIp, int vlan) {
  if (out != NULL) {
    if (pvMac) strncpy(out->privateMac,pvMac,24);
    if (pbMac) strncpy(out->publicMac,pbMac,24);
    if (pvIp) strncpy(out->privateIp,pvIp,24);
    if (pbIp) strncpy(out->publicIp,pbIp,24);
    out->vlan = vlan;
  }
  return(0);
}

int allocate_virtualMachine(virtualMachine *out, int mem, int disk, int cores, char *name) {
  if (out != NULL) {
    out->mem = mem;
    out->disk = disk;
    out->cores = cores;
    snprintf(out->name, 64, "%s", name);
  }
  return(0);
}

void print_instanceCache(void) {
  int i;

  sem_wait(instanceCacheLock);
  for (i=0; i<MAXINSTANCES; i++) {
    if (instanceCache->valid[i]) {
      logprintfl(EUCADEBUG,"\tcache: %d/%d %s %s %s %s\n", i, instanceCache->numInsts, instanceCache->instances[i].instanceId, instanceCache->instances[i].ccnet.publicIp, instanceCache->instances[i].ccnet.privateIp, instanceCache->instances[i].state);
    }
  }
  sem_post(instanceCacheLock);
}

void invalidate_instanceCache(void) {
  int i;
  
  sem_wait(instanceCacheLock);
  bzero(instanceCache->valid, sizeof(int)*MAXINSTANCES);
  instanceCache->numInsts = 0;
  instanceCache->instanceCacheUpdate = 0;
  /*  instanceCache->numInsts = 0;
  for (i=0; i<MAXINSTANCES; i++) {
    if (instanceCache->instances[i].instanceId[0] != '\0') {
      // del from cache
      instanceCache->instances[i].instanceId[0] = '\0';
    }
  }
  */
  sem_post(instanceCacheLock);
  
}

int refresh_instanceCache(char *instanceId, ccInstance *in){
  int i, done, rc;
  
  if (!instanceId || !in) {
    return(1);
  }
  
  sem_wait(instanceCacheLock);
  done=0;
  for (i=0; i<MAXINSTANCES && !done; i++) {
    if (instanceCache->valid[i]) {
      if (!strcmp(instanceCache->instances[i].instanceId, instanceId)) {
	// in cache
	logprintfl(EUCADEBUG, "refreshing instance '%s'\n", instanceId);
	memcpy(&(instanceCache->instances[i]), in, sizeof(ccInstance));
	sem_post(instanceCacheLock);
	return(0);
      }
    }
  }
  sem_post(instanceCacheLock);

  add_instanceCache(instanceId, in);

  return(0);
}

int add_instanceCache(char *instanceId, ccInstance *in){
  int i, done, firstNull=0;

  if (!instanceId || !in) {
    return(1);
  }
  
  sem_wait(instanceCacheLock);
  done=0;
  for (i=0; i<MAXINSTANCES && !done; i++) {
    if (instanceCache->valid[i]) {
      if (!strcmp(instanceCache->instances[i].instanceId, instanceId)) {
	// already in cache
	sem_post(instanceCacheLock);
	return(0);
      }
    } else {
      firstNull = i;
      done++;
    }
  }
  instanceCache->valid[firstNull] = 1;
  allocate_ccInstance(&(instanceCache->instances[firstNull]), in->instanceId, in->amiId, in->kernelId, in->ramdiskId, in->amiURL, in->kernelURL, in->ramdiskURL, in->ownerId, in->state, in->ts, in->reservationId, &(in->ccnet), &(in->ccvm), in->ncHostIdx, in->keyName, in->serviceTag, in->userData, in->launchIndex, in->groupNames, in->volumes, in->volumesSize, in->networkIndex);
  instanceCache->numInsts++;
  sem_post(instanceCacheLock);
  return(0);
}

int del_instanceCacheId(char *instanceId) {
  int i;

  sem_wait(instanceCacheLock);
  for (i=0; i<MAXINSTANCES; i++) {
    if (instanceCache->valid[i]) {
      if (!strcmp(instanceCache->instances[i].instanceId, instanceId)) {
	// del from cache
	bzero(&(instanceCache->instances[i]), sizeof(ccInstance));
	instanceCache->valid[i] = 0;
	instanceCache->numInsts--;
	sem_post(instanceCacheLock);
	return(0);
      }
    }
  }
  sem_post(instanceCacheLock);
  return(0);
}

int find_instanceCacheId(char *instanceId, ccInstance **out) {
  int i, done;
  
  if (!instanceId || !out) {
    return(1);
  }
  
  sem_wait(instanceCacheLock);
  *out = NULL;
  done=0;
  for (i=0; i<MAXINSTANCES && !done; i++) {
    if (instanceCache->valid[i]) {
      if (!strcmp(instanceCache->instances[i].instanceId, instanceId)) {
	// found it
	*out = malloc(sizeof(ccInstance));
	allocate_ccInstance(*out, instanceCache->instances[i].instanceId,instanceCache->instances[i].amiId, instanceCache->instances[i].kernelId, instanceCache->instances[i].ramdiskId, instanceCache->instances[i].amiURL, instanceCache->instances[i].kernelURL, instanceCache->instances[i].ramdiskURL, instanceCache->instances[i].ownerId, instanceCache->instances[i].state,instanceCache->instances[i].ts, instanceCache->instances[i].reservationId, &(instanceCache->instances[i].ccnet), &(instanceCache->instances[i].ccvm), instanceCache->instances[i].ncHostIdx, instanceCache->instances[i].keyName, instanceCache->instances[i].serviceTag, instanceCache->instances[i].userData, instanceCache->instances[i].launchIndex, instanceCache->instances[i].groupNames, instanceCache->instances[i].volumes, instanceCache->instances[i].volumesSize, instanceCache->instances[i].networkIndex);
	done++;
      }
    }
  }

  sem_post(instanceCacheLock);
  if (done) {
    return(0);
  }
  return(1);
}

int find_instanceCacheIP(char *ip, ccInstance **out) {
  int i, done;
  
  if (!ip || !out) {
    return(1);
  }
  
  sem_wait(instanceCacheLock);
  *out = NULL;
  done=0;
  for (i=0; i<MAXINSTANCES && !done; i++) {
    if (instanceCache->valid[i] && (instanceCache->instances[i].ccnet.publicIp[0] != '\0' || instanceCache->instances[i].ccnet.privateIp[0] != '\0')) {
      if (!strcmp(instanceCache->instances[i].ccnet.publicIp, ip) || !strcmp(instanceCache->instances[i].ccnet.privateIp, ip)) {
	// found it
	*out = malloc(sizeof(ccInstance));
	allocate_ccInstance(*out, instanceCache->instances[i].instanceId,instanceCache->instances[i].amiId, instanceCache->instances[i].kernelId, instanceCache->instances[i].ramdiskId, instanceCache->instances[i].amiURL, instanceCache->instances[i].kernelURL, instanceCache->instances[i].ramdiskURL, instanceCache->instances[i].ownerId, instanceCache->instances[i].state,instanceCache->instances[i].ts, instanceCache->instances[i].reservationId, &(instanceCache->instances[i].ccnet), &(instanceCache->instances[i].ccvm), instanceCache->instances[i].ncHostIdx, instanceCache->instances[i].keyName, instanceCache->instances[i].serviceTag, instanceCache->instances[i].userData, instanceCache->instances[i].launchIndex, instanceCache->instances[i].groupNames, instanceCache->instances[i].volumes, instanceCache->instances[i].volumesSize, instanceCache->instances[i].networkIndex);
	done++;
      }
    }
  }

  sem_post(instanceCacheLock);
  if (done) {
    return(0);
  }
  return(1);
}

