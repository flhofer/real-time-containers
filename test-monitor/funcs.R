# Common functions and settings

tests <- list()
group1 <- c("1-1", "1-2", "1-3", "1-4", "1-5", "1-6", "1-7", "1-8", "1-9", "1-10")
group2 <- c("2-1", "2-2")
group3 <- c("3-1", "3-2", "3-3")
group4 <- c("4-1", "4-2", "4-3", "4-4", "4-5", "4-6", "4-7", "4-8", "4-9", "4-10")
tests  <- cbind( tests, list(group1, group2, group3, group4))

machines <- c("C5", "BM" , "T3", "T3U")

getDataPars <- function(records) {
	recfiltered <- records[!(records$RunT > records$cDur*1.5),] # Dump all values higher than 1.5*  programmed duration (filter of overshoots)

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
	prdMaxP = max(records$Period)
	pcount = sum ( records$Period > prdAvg*1.5) # WARING! could loose some results!!

	return (data.frame (runMin=runMin, runMdn=runMdn, Avg=runAvg, runDif=runDif, runStD=runStD, runMax=runMax,
		prdMin=prdMin, prdMdn=prdMdn, prdAvg=prdAvg, prdStD=prdStD, prdMax=prdMax, prdMaxP=prdMaxP, pcount))
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
	records <- data.frame ("RunT"=records$V3,"Period"=records$V4,"rStart"=records$V7, "cDur"=records$V9)

	return(records)
}
