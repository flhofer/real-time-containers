# Common functions and settings

tests <- list()
group1 <- c("1-1", "1-2", "1-3", "1-4", "1-5", "1-6", "1-7", "1-8", "1-9", "1-10")
group2 <- c("2-1", "2-2")
group3 <- c("3-1", "3-2", "3-3")
group4 <- c("4-1", "4-2", "4-3", "4-4", "4-5", "4-6", "4-7", "4-8", "4-9", "4-10")
tests  <- cbind( tests, list(group1, group2, group3, group4))

machines <- c("C5", "BM" , "T3", "T3U")

loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	# cat (fName, "\n")

	if (!file.exists(fName) || file.info(fName)$size == 0) {
		# break here if file does not exist
		dat <- data.frame ("RunT"=numeric(),"Period"=numeric(),"rStart"=numeric(), "cDur"=numeric())
		return(dat)
	}

	# Transform into table, filter and add names
	dat = read.table(file= fName)
	dat <- data.frame ("RunT"=dat$V3,"Period"=dat$V4,"rStart"=dat$V7, "cDur"=dat$V9)

	return(dat)
}
