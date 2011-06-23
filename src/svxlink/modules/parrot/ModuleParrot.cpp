/**
@file	 ModuleParrot.cpp
@brief   A module that implements a "parrot" function.
@author  Tobias Blomberg / SM0SVX
@date	 2004-03-21

This module implements a "parrot" function. It plays back everything you say
to it. This can be used as a simplex repeater or just so you can hear how
you sound.

\verbatim
A module (plugin) for the multi purpose tranciever frontend system.
Copyright (C) 2004-2008  Tobias Blomberg

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
\endverbatim
*/



/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncTimer.h>
#include <AsyncAudioFifo.h>
#include <AsyncAudioStreamStateDetector.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "version/MODULE_PARROT.h"
#include "ModuleParrot.h"



/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;



/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local class definitions
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Prototypes
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/




/****************************************************************************
 *
 * Local Global Variables
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Pure C-functions
 *
 ****************************************************************************/


extern "C" {
  Module *module_init(void *dl_handle, Logic *logic, const char *cfg_name)
  {
    return new ModuleParrot(dl_handle, logic, cfg_name);
  }
} /* extern "C" */



/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/


ModuleParrot::ModuleParrot(void *dl_handle, Logic *logic,
      	      	      	   const string& cfg_name)
  : Module(dl_handle, logic, cfg_name), state_det(0), fifo(0),
    squelch_is_open(false), repeat_delay(0), repeat_delay_timer(0)
{
  cout << "\tModule Parrot v" MODULE_PARROT_VERSION " starting...\n";
  
} /* ModuleParrot */


ModuleParrot::~ModuleParrot(void)
{
  AudioSink::clearHandler();
  AudioSource::clearHandler();
  delete state_det; // This will delete all chained audio objects
} /* ~ModuleParrot */


bool ModuleParrot::initialize(void)
{
  if (!Module::initialize())
  {
    return false;
  }
  
  string fifo_len;
  if (!cfg().getValue(cfgName(), "FIFO_LEN", fifo_len))
  {
    cerr << "*** Error: Config variable " << cfgName() << "/FIFO_LEN not set\n";
    return false;
  }
  string value;
  if (cfg().getValue(cfgName(), "REPEAT_DELAY", value))
  {
    repeat_delay = atoi(value.c_str());
  }
  
  state_det = new AudioStreamStateDetector;
  state_det->sigStreamStateChanged.connect(
     slot(*this, &ModuleParrot::onStreamStateChanged));
  AudioSink::setHandler(state_det);
  
  fifo = new AudioFifo(atoi(fifo_len.c_str())*INTERNAL_SAMPLE_RATE);
  fifo->enableOutput(false);
  state_det->registerSink(fifo, true);
  
  AudioSource::setHandler(fifo);
  
  return true;
  
} /* ModuleParrot::initialize */






/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/


void ModuleParrot::logicIdleStateChanged(bool is_idle)
{
  /*
  printf("ModuleParrot::logicIdleStateChanged: is_idle=%s fifo->empty()=%s\n",
      is_idle ? "TRUE" : "FALSE",
      fifo->empty() ? "TRUE" : "FALSE");
  */
  Module::logicIdleStateChanged(is_idle);
  
  if (is_idle)
  {
    if (!fifo->empty())
    {
      if (repeat_delay > 0)
      {
      	repeat_delay_timer = new Timer(repeat_delay);
	repeat_delay_timer->expired.connect(
	    slot(*this, &ModuleParrot::onRepeatDelayExpired));
      }
      else
      {
      	onRepeatDelayExpired(0);
      }
    }
    else if (!cmd_queue.empty())
    {
      execCmdQueue();
    }
  }
  else
  {
    delete repeat_delay_timer;
    repeat_delay_timer = 0;
  }
} /* ModuleParrot::logicIdleStateChanged */




/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/


/*
 *----------------------------------------------------------------------------
 * Method:    activateInit
 * Purpose:   Called by the core system when this module is activated.
 * Input:     None
 * Output:    None
 * Author:    Tobias Blomberg / SM0SVX
 * Created:   2004-03-07
 * Remarks:   
 * Bugs:      
 *----------------------------------------------------------------------------
 */
void ModuleParrot::activateInit(void)
{
  fifo->clear();
  cmd_queue.clear();
  fifo->enableOutput(false);
} /* activateInit */


/*
 *----------------------------------------------------------------------------
 * Method:    deactivateCleanup
 * Purpose:   Called by the core system when this module is deactivated.
 * Input:     None
 * Output:    None
 * Author:    Tobias Blomberg / SM0SVX
 * Created:   2004-03-07
 * Remarks:   Do NOT call this function directly unless you really know what
 *    	      you are doing. Use Module::deactivate() instead.
 * Bugs:      
 *----------------------------------------------------------------------------
 */
void ModuleParrot::deactivateCleanup(void)
{
  fifo->enableOutput(true);
  fifo->clear();
  delete repeat_delay_timer;
  repeat_delay_timer = 0;
} /* deactivateCleanup */


/*
 *----------------------------------------------------------------------------
 * Method:    dtmfDigitReceived
 * Purpose:   Called by the core system when a DTMF digit has been
 *    	      received.
 * Input:     digit   	- The DTMF digit received (0-9, A-D, *, #)
 *    	      duration	- The duration in milliseconds
 * Output:    Return true if the digit is handled or false if not
 * Author:    Tobias Blomberg / SM0SVX
 * Created:   2004-03-07
 * Remarks:   
 * Bugs:      
 *----------------------------------------------------------------------------
 */
bool ModuleParrot::dtmfDigitReceived(char digit, int duration)
{
  //cout << "DTMF digit received in module " << name() << ": " << digit << endl;
  
  return false;
  
} /* dtmfDigitReceived */


/*
 *----------------------------------------------------------------------------
 * Method:    dtmfCmdReceived
 * Purpose:   Called by the core system when a DTMF command has been
 *    	      received. A DTMF command consists of a string of digits ended
 *    	      with a number sign (#). The number sign is not included in the
 *    	      command string.
 * Input:     cmd - The received command.
 * Output:    None
 * Author:    Tobias Blomberg / SM0SVX
 * Created:   2004-03-07
 * Remarks:   
 * Bugs:      
 *----------------------------------------------------------------------------
 */
void ModuleParrot::dtmfCmdReceived(const string& cmd)
{
  cout << "DTMF command received in module " << name() << ": " << cmd << endl;
  
  cmd_queue.push_back(cmd);
  if (fifo->empty() && !squelch_is_open)
  {
    execCmdQueue();
  }
} /* dtmfCmdReceived */


void ModuleParrot::dtmfCmdReceivedWhenIdle(const std::string &cmd)
{
  stringstream ss;
  ss << "spell_digits " << cmd;
  processEvent(ss.str());
} /* dtmfCmdReceivedWhenIdle */


void ModuleParrot::squelchOpen(bool is_open)
{
  squelch_is_open = is_open;
} /* ModuleParrot::squelchOpen */


void ModuleParrot::onStreamStateChanged(bool is_active, bool is_idle)
{
  //cout << "ModuleParrot::onStreamStateChanged\n";
  
  if (is_idle)
  {
    if (!cmd_queue.empty())
    {
      execCmdQueue();
    }
    fifo->enableOutput(false);
  }
  
} /* ModuleParrot::onStreamStateChanged */


void ModuleParrot::onRepeatDelayExpired(Timer *t)
{
  delete repeat_delay_timer;
  repeat_delay_timer = 0;
  
  fifo->enableOutput(true);
  
} /* ModuleParrot::onRepeatDelayExpired */


void ModuleParrot::execCmdQueue(void)
{
  //printf("ModuleParrot::execCmdQueue\n");
  
  list<string> cq = cmd_queue;
  cmd_queue.clear();
  
  list<string>::iterator it;
  for (it=cq.begin(); it!=cq.end(); ++it)
  {
    string cmd(*it);
    
    if (cmd == "")
    {
      deactivateMe();
    }
    else
    {
      if (cmd == "0")
      {
	playHelpMsg();
      }
      else
      {
      	stringstream ss;
	ss << "spell_digits " << cmd;
      	processEvent(ss.str());
      }
    }
  }
} /* ModuleParrot::execCmdQueue */




/*
 * This file has not been truncated
 */
