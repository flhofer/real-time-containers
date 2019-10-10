# Set the working directory
setwd("./container/pretest/")
options("width"=200)

loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	# cat (fName, "\n")

	if (!file.exists(fName)) {
		# break here if file does not exist
		dat <- data.frame("RunT"=numeric(),"Period"=numeric(),"rStart"=numeric())
		return(dat)
	}

	if (file.info(fName)$size > 0) {
		# Transform into table, filter and add names
		dat = read.table(file= fName)
		#dat = read.table(text = tFile)
		dat <- data.frame ("RunT"=dat$V3,"Period"=dat$V4,"rStart"=dat$V7, "cDur"=dat$V9)
	}
	else {
		dat <- data.frame ("RunT"=numeric(),"Period"=numeric(),"rStart"=numeric(), "cDur"=numeric())
	}
	
	return(dat)
}

#machines <- c("C5", "BM" , "T3", "T3U")
#types <- c("dyntick", "fixtick")
#tests <- c("1-1", "1-2", "1-3", "1-4", "1-5", "1-6", "1-7", "1-8", "1-9", "1-10", "2-1", "2-2", "3-1", "3-2", "3-3",
# "4-1", "4-2", "4-3", "4-4", "4-5", "4-6", "4-7", "4-8", "4-9", "4-10")
machines <- c("T3", "T3U", "C5", "BM")
types <- c("test1", "test2", "test3", "test4", "test5", "test6", "test7", "test8")
#types <- c("test1")
tests <- c("1-1", "1-2", "1-3", "1-4", "1-5", "1-6", "1-7", "1-8", "1-9", "1-10")

for (i in 1:length(machines)) {
	for (j in 1:length(types)) {

		testPcount = 0;
		for (k in 1:length(tests)) {

			# Label and directory pattern
			cat(machines[i], "-", types[j], "-", tests[k], "\n")
			
			# Find all directories with pattern.. 
			dir = paste0(machines[i], "/", types[j], "/", tests[k])

			maxAll = 0
			pcountAll = 0

			r<- data.frame (matrix(ncol=11,nrow=0))
			names(r) <- c("Container","Min", "Avg", "AvgDev", "Max", "pMin", "pAvg", "pavgDev", "pMax", "pOVPeak", "pcount")
			nr = 0
			files <- list.files(path=dir, pattern="*.log", full.names=TRUE, recursive=FALSE)
			for (x in files) {

				nr = nr +1
				dat <- loadData(x)
				datp <- dat[!(dat$RunT > dat$cDur*1.5),]

				minMin = min(datp$RunT, na.values=FALSE)
				avgMea = mean(datp$RunT, na.values=FALSE)
				avgDev = sqrt(var(datp$RunT))
				maxMax = max(datp$RunT, na.values=FALSE)
				pminMin = min(datp$Period, na.values=FALSE)
				pavgMea = mean(datp$Period, na.values=FALSE)
				pavgDev = sqrt(var(datp$Period))
				pmaxMax = max(datp$Period, na.values=FALSE)
				pmaxMaxp = max(dat$Period, na.values=FALSE)
				pcount = sum ( dat$Period > pavgMea*1.5)
				maxAll = max(maxMax, maxAll)
				pcountAll = pcountAll + pcount

				r[nrow(r)+1,] <-data.frame (nr, minMin, avgMea, avgDev, maxMax, pminMin, pavgMea, pavgDev, pmaxMax, pmaxMaxp, pcount)

			}
			print(r)
			cat ("Peak - Peak count ", maxAll, pcountAll, "\n")
			testPcount = testPcount + pcountAll
  		}
		cat ("Test set overruns ", testPcount , "\n")
		cat ("----------------------------------------\n")
	}
}
