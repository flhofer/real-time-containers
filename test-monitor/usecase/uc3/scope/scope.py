'''
Created on Jun 12, 2024

@author:     Florian Hofer

@copyright:  2024 Florian Hofer. All rights reserved.

@license:    GPLv3

@contact:    info@florianhofer.it
@deffield    updated: 2024-06-12
'''

import vxi11
import re

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
            self._instr.timeout = 1000
            print("Connected to :  ", self._instr.ask("*IDN?"))
            print("Status : ", self._instr.ask("ALST?"))
        except Exception as e:
            raise (e) 
        
        
    def setScreen(self):
        '''
        Set screen and channel values to match our display area
        24V pulsing singal at ~1KHz
        '''
        
        self._instr.ask("C2:TRA ON")    # enable channel 2

        self._instr.ask("C1:VDIV 3.5V") # 8 Div's total = 28Vpp
        self._instr.ask("C2:VDIV 3.5V") # 8 Div's total = 28Vpp

        self._instr.ask("TDIV 100us")   # Time division hor, 16 divs= 1,6 ms
        
        self._instr.ask("C1:OFST -12V") # Offset Ver
        self._instr.ask("C2:OFST -12V") # Offset Ver
        
        self._instr.ask("TDIV 50us")    # set to 50us hor div
        self._instr.ask("TRDL 350us")   # set h offset to 350 to allow right slack..

        self._instr.ask("C1:TRSL POS")  # Positive trigger
        self._instr.ask("C2:TRSL POS")  # Positive trigger

        self._instr.ask("SET50")        # Set trigger level to 50% pp 
        
        self._instr.ask("C1:ATTN 1")    # Set probe attenuation to 1x
        self._instr.ask("C2:ATTN 1")    # Set probe attenuation to 1x        
    
    def checkSampleRate(self):
        '''
        Verify if the sample rate is high enough to find glitches
        '''
        
        rateStr=self._instr.ask("SARA?").removeprefix("SARA ")
        prs = re.compile('([0-9.]+)\s*(\w+)')
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
                
    def setCursors(self):
        '''
        Set cursors and/or measurements to perform on the input signal
        '''
        
        self._instr.ask("MEAD FRR,C1-C2")   # set delay measurement first rising edge to first rising edge
        self._instr.ask("MEAD LFF,C1-C2")   # set delay measurement last falling edge to last falling edge
        
    def measureJitter(self):
        ''' 
        Ask the instrument to measure the delay between channels
        -> use FRR = difference time
        '''
        #TODO: pass to func
        delayStr= self._instr.ask("C1-C2 MEAD? FRR").split(",", 1)[1]   # read value
        prs = re.compile('([0-9.]+)\s*(\w+)')
        delayBase, delaySuff = prs.match(delayStr).groups()
        delay = float(delayBase)
        if 'n' == delaySuff[0]:
            delay /= 1000000000
        if 'u' == delaySuff[0]:
            delay /= 1000000
        if 'm' == delaySuff[0]:
            delay /= 1000
            
        return delay
    