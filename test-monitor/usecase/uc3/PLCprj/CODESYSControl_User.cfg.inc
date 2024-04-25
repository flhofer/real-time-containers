[CmpLog]
Logger.1.Name=tasktimes.log
Logger.1.Filter=0x00000010
Logger.1.Enable=1
Logger.1.MaxEntries=100000
Logger.1.MaxFileSize=1000000
Logger.1.MaxFiles=10
Logger.1.Type=0x344 ;Set the timestamp to us Tick - 314 with RTC is default
Logger.1.Backend.0.ClassId=0x00000104 ;writes logger messages in a file

