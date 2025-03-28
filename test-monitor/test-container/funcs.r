# Common functions and settings

getDataPars <- function(records) {
	# Dump all values higher than 1.5* avg period, count points exceeding
	prdAvg = mean(records$Period)
	prdMaxP = max(records$Period)
	pcount = sum ( floor ((records$Period - prdAvg*0.5 )/ prdAvg) ) # WARING! could loose some results!!
	recfiltered <- records[!(records$Period > prdAvg*1.5),] 
	
	runMin = min(recfiltered$RunT)
	runAvg = mean(recfiltered$RunT)
	runMdn = median(recfiltered$RunT)
	runStD = sqrt(var(recfiltered$RunT))
	runDif = runMdn-runAvg;
	runMax = max(recfiltered$RunT)

	prdMin = min(recfiltered$Period)
	prdAvg = mean(recfiltered$Period) 
	prdMdn = median(recfiltered$Period)
	prdStD = sqrt(var(recfiltered$Period))
	prdMax = max(recfiltered$Period)

	return (data.frame (runMin, runMdn, runAvg, runDif, runStD, runMax, prdMin, prdMdn, prdAvg, prdStD, prdMax, prdMaxP, pcount))
}

loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	# cat (fName, "\n")

	if (!file.exists(fName) || file.info(fName)$size == 0) {
		# break here if file does not exist
		records <- data.frame ("RunT"=numeric(),"Period"=numeric(),"rStart"=numeric(), "cDur"=numeric())
		return(records)
	}

	# Transform into table, filter and add names
	records = read.table(file= fName)
	records <- data.frame ("RunT"=records$V3,"Period"=records$V4, "Start"=records$V5, "rStart"=records$V7, "cDur"=records$V9)

	return(records)
}
