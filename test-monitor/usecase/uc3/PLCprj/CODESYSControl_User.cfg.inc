[CmpLog]
Logger.1.Name=tasktimes.log
Logger.1.Filter=0x00000010
Logger.1.Enable=1
Logger.1.MaxEntries=200000
Logger.1.MaxFileSize=10000000
Logger.1.MaxFiles=10
Logger.1.Type=0x344 ;Set the timestamp to us Tick - 314 with RTC is default
Logger.1.Backend.0.ClassId=0x00000104 ;writes logger messages in a file

[SysCpuHandling]
Linux.DisableCpuDmaLatency=1

[SysSocket]
Adapter.0.Name="enp2s0"
Adapter.0.EnableSetIpAndMask=1
Adapter.1.Name="enp4s0"
Adapter.1.EnableSetIpAndMask=1

