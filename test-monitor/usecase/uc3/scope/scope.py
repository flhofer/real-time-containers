'''
Created on Jun 12, 2024

@author:     Florian Hofer

@copyright:  2024 Florian Hofer. All rights reserved.

@license:    GPLv3

@contact:    info@florianhofer.it
@deffield    updated: 2024-06-12
'''

import vxi11
from sympy.codegen.ast import none

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
        except Exception as e:
            raise (e) 
        
        
    def setScreen(self):
        
        print(self._instr.ask("ALST?"))
        
    def setChannels(self):
        pass
        
        
    def measureJitter(self):

        return 0