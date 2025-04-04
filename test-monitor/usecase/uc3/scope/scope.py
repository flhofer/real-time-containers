'''
Created on Jun 12, 2024

@author:     Florian Hofer

@copyright:  2024 Florian Hofer. All rights reserved.

@license:    GPLv3

@contact:    info@florianhofer.it
@deffield    updated: 2025-03-31
'''

import vxi11
import re
from time import sleep

class Scope(object):
    '''
    scope interface setter, uses VXI-11
    '''


    def __init__(self, ip_addr):
        '''
        Constructor
        '''
        try:
            self._instr =  vxi11.Instrument(ip_addr)
            self._instr.timeout = 10    # set timeout to 10 seconds (default)
            print("Connected to :  ", self._instr.ask("*IDN?"))
            print("Status : ", self._instr.ask("ALST?"))
        except Exception as e:
            raise (e) 
        
        
    def setScreen(self):
        '''
        Set screen and channel values to match our display area
        24V pulsing singal at ~1KHz - Default values for 2chn same screen
        '''

        self._instr.ask("PESU 1")       # set persistence to 1sec
        self._instr.ask("PERS OFF")     # disable persistence
        self._instr.ask("C2:TRA ON")    # enable channel 2

        self._instr.ask("C1:TRSL POS")  # Positive trigger
        # self._instr.ask("C2:TRSL POS")  # Positive trigger

        self._instr.ask("SET50")        # Set trigger level to 50% pp 
        
        self._instr.ask("C1:ATTN 1")    # Set probe attenuation to 1x
        self._instr.ask("C2:ATTN 1")    # Set probe attenuation to 1x       
        
        self._instr.ask("SCSV YES")     # set screen saver 

        self._instr.ask("TRMD AUTO")    # Start acquisition

    def clearScreen(self):
        '''
        Clear screen 
        '''

        self._instr.ask("PACL")         # reset all custom parameters/screen/persistence
        
    def setChannels(self, prg_prd=1):
        '''
        Set screen and channel values to match our display area
        24V pulsing Singal at 5Hz
        prg_prd: program period defines PLC main cycle update in ms
        '''
        
        self._prg_prd=prg_prd
        
        #TODO: change to string-list passed as one
        self._instr.ask("C2:TRA ON")    # enable channel 2

        self._instr.ask("C1:VDIV 10V")  # set to upper half
        self._instr.ask("C1:OFST 10V")  # Offset vertical

        self._instr.ask("TDIV {0}ms".format(prg_prd))     # Time division horizontal 5 ms
        self._instr.ask("TRDL {0}ms".format(prg_prd * 3))    # set h offset to 350 to allow right slack..

        self._instr.ask("C1:TRLV 12V")  # Trigger half, voltage
        self._instr.ask("C2:VDIV 10V")  # set to lower half
        self._instr.ask("C2:OFST -30V") # Offset vertical

        # self._instr.ask("PACL")         # reset all custom parameters
        # self._instr.ask("PACU PER,C1")  # add period to parameters for channel 1
        
        self._instr.ask("PESU Infinite")  # set infinite persistence
        self._instr.ask("PERS ON")        # set persistence on

    def checkSampleRate(self):
        '''
        Verify if the sample rate is high enough to find glitches
        '''
        
        rateStr=self._instr.ask("SARA?").removeprefix("SARA ")
        prs = re.compile('([0-9.]+)\\s*(\\w+)')
        rateBase, rateSuff = prs.match(rateStr).groups()
        rate = float(rateBase)
        if 'G' == rateSuff[0]:
            rate *= 1000000000
        if 'M' == rateSuff[0]:
            rate *= 1000000
        if 'K' == rateSuff[0]:
            rate *= 1000
        
        freqStr = self._instr.ask("CYMT?")
        freqBase, freqSuff = prs.match(freqStr).groups()
        freq = float(freqBase)
        if 'G' == freqSuff[0]:
            freq *= 1000000000
        if 'M' == freqSuff[0]:
            freq *= 1000000
        if 'K' == freqSuff[0]:
            freq *= 1000
        
        if rate > 5000 * freq:
            raise 

    def setFileName(self, number, basename="Wave", sto_type="CSV"):

        self._sto_type=sto_type
        
        if sto_type ==  "USB":
            # PARAMETERS FOR USB STORE
            self._instr.ask("DIR DISK,UDSK,CREATE,'/vplctest/'")
            print(self._instr.ask("ALST?"))
            self._instr.ask("FLNM TYPE,C1,FILE,'settest"+str(number)+"'")
            self._instr.ask("STST C1,UDSK")

        self._fname = basename + str(number)
        
    def storeWaveform(self):

        if self._sto_type == "CSV":       
        # store CSV data points 10 times
            file1 = open(self._fname +  ".csv", "a")
            file1.write(self._instr.ask("GET_CSV? DD, DIS, SAVE, OFF"))
            file1.close()

        elif self._sto_type == "USB":
            # STORE AS USB DATA INSTEAD?
            self._instr.ask("STO C1,UDSK")
            self._instr.ask("WFSU SP,0,NP,20000,FP,0")

        elif self._sto_type == "RAW":
            # RAW DATA WRITE TO FILE
            file1 = open(self._fname + "-C1.dat", "ab")
            self._instr.write("C1:WF? ALL")
            file1.write(self._instr.read_raw())
            file1.close()

            file1 = open(self._fname + "-C2.dat", "ab")
            self._instr.write("C2:WF? ALL")
            file1.write(self._instr.read_raw())
            file1.close()

    def storeScreen(self):

        self._instr.ask("MENU OFF")    # Hide Menu for Screenshot
        sleep(0.5)
        
        # Store wave screenshot
        file1 = open(self._fname + ".jpg", "wb")
        self._instr.write("SCDP")
        file1.write(self._instr.read_raw())
        file1.close()
                
    def setCursors(self):
        '''
        Set cursors and/or measurements to perform on the input signal
        '''
        
        self._instr.ask("C2:CRST HREF,{0},HDIF,{1}".format(7+0.25/self._prg_prd, 8+0.25/self._prg_prd))    # set cursor to 2 divs (+0.3) right of trigger
        
        #FIXME: instrument call does nothing
        # self._instr.ask("MEAD FRR,C1-C2")   # set delay measurement first rising edge to first rising edge
        # self._instr.ask("MEAD LFF,C1-C2")   # set delay measurement last falling edge to last falling edge
        
    def measureJitter(self):
        ''' 
        Ask the instrument to measure the delay between channels
        -> use FRR = difference time
        '''
        #FIXME: instrument call causes exception
        #TODO: pass to func
        delayStr= self._instr.ask("C1-C2 MEAD? FRF").split(",", 1)[1]   # read value
        prs = re.compile('([0-9.]+)\\s*(\\w+)')
        delayBase, delaySuff = prs.match(delayStr).groups()
        delay = float(delayBase)
        if 'n' == delaySuff[0]:
            delay /= 1000000000
        if 'u' == delaySuff[0]:
            delay /= 1000000
        if 'm' == delaySuff[0]:
            delay /= 1000
            
        return delay
    
    def getperiod(self):
        pass
