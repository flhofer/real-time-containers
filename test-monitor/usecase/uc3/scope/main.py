#!/usr/local/bin/python2.7
# encoding: utf-8
'''
main -- shortdesc

main is a scope connector to program 'skippy' compatible oscilloscopes for jitter analysis

@author:     Florian Hofer

@copyright:  2024 Florian Hofer. All rights reserved.

@license:    GPLv3

@contact:    info@florianhofer.it
@deffield    updated: 2024-06-12
'''

import sys
import os
import scope

from argparse import ArgumentParser
from argparse import RawDescriptionHelpFormatter

__all__ = []
__version__ = 0.1
__date__ = '2024-06-12'
__updated__ = '2024-06-12'

DEBUG = 1
TESTRUN = 0
PROFILE = 0

# Scope object

def startScope(ip_addr):
    
    s = scope.Scope(ip_addr)

    # Preparation for aquisition
    s.setScreen()

    return s 

def testScope(ip_addr):

    s = startScope(ip_addr)
    s.setChannels(0.25)
    
    s.setFileName(1)
    
    s.setCursors()
    # print(s.measureJitter())

    # save to file?
    s.storeWaveform()

    # repeat until interrupted
    


class CLIError(Exception):
    '''Generic exception to raise and log different fatal errors.'''
    def __init__(self, msg):
        super(CLIError).__init__(type(self))
        self.msg = "E: %s" % msg
    def __str__(self):
        return self.msg
    def __unicode__(self):
        return self.msg

def main(argv=None): # IGNORE:C0111
    '''Command line options.'''

    if argv is None:
        argv = sys.argv
    else:
        sys.argv.extend(argv)

    program_name = os.path.basename(sys.argv[0])
    program_version = "v%s" % __version__
    program_build_date = str(__updated__)
    program_version_message = '%%(prog)s %s (%s)' % (program_version, program_build_date)
    program_shortdesc = __import__('__main__').__doc__.split("\n")[1]
    program_license = '''%s

  Created by Florian Hofer on %s.
  Copyright 2024 organization_name. All rights reserved.

  Licensed under the General Public License 3.0
  https://www.gnu.org/licenses/gpl-3.0.en.html

  Distributed on an "AS IS" basis without warranties
  or conditions of any kind, either express or implied.

USAGE
''' % (program_shortdesc, str(__date__))

    try:
        # Setup argument parser
        parser = ArgumentParser(description=program_license, formatter_class=RawDescriptionHelpFormatter)
        # parser.add_argument("-r", "--recursive", dest="recurse", action="store_true", help="recurse into subfolders [default: %(default)s]")
        # parser.add_argument("-v", "--verbose", dest="verbose", action="count", help="set verbosity level [default: %(default)s]")
        # parser.add_argument("-i", "--include", dest="include", help="only include paths matching this regex pattern. Note: exclude is given preference over include. [default: %(default)s]", metavar="RE" )
        # parser.add_argument("-e", "--exclude", dest="exclude", help="exclude paths matching this regex pattern. [default: %(default)s]", metavar="RE" )
        # parser.add_argument('-V', '--version', action='version', version=program_version_message)
        parser.add_argument(dest="ip_addr", help="IP-address of the VXI-11 compatible oscilloscope", metavar="ip_addr")

        # Process arguments
        args = parser.parse_args()

        ip_addr = args.ip_addr
        # verbose = args.verbose
        # recurse = args.recurse
        # inpat = args.include
        # expat = args.exclude

        # if verbose > 0:
        #     print("Verbose mode on")
        #     if recurse:
        #         print("Recursive mode on")
        #     else:
        #         print("Recursive mode off")
        #
        # if inpat and expat and inpat == expat:
        #     raise CLIError("include and exclude pattern are equal! Nothing will be processed.")

        # for inpath in paths:
        #     ### do something with inpath ###
        #     print(inpath)
            
        testScope(ip_addr)
        return 0
    except KeyboardInterrupt:
        ### handle keyboard interrupt ###
        return 0
    except Exception as e:
        if DEBUG or TESTRUN:
            raise(e)
        indent = len(program_name) * " "
        sys.stderr.write(program_name + ": " + repr(e) + "\n")
        sys.stderr.write(indent + "  for help use --help")
        return 2

if __name__ == "__main__":   
    # if DEBUG:
    #     sys.argv.append("-h")
        # sys.argv.append("-v")
        # sys.argv.append("-r")
    if TESTRUN:
        import doctest
        doctest.testmod()
    if PROFILE:
        import cProfile
        import pstats
        profile_filename = 'main_profile.txt'
        cProfile.run('main()', profile_filename)
        statsfile = open("profile_stats.txt", "wb")
        p = pstats.Stats(profile_filename, stream=statsfile)
        stats = p.strip_dirs().sort_stats('cumulative')
        stats.print_stats()
        statsfile.close()
        sys.exit(0)
    sys.exit(main())
    
