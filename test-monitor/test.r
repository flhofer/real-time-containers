# Set the working directory
setwd("./container/")
datacnt = 13

loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	#cat (fName, "\n")

	if (!file.exists(fName)) {
		# break here if file does not exist
		dat <- data.frame("RunT"=numeric(),"Period"=numeric(),"rStart"=numeric())
		return(dat)
	}

	# Transform into table, filter and add names
	dat = read.table(file= fName)
	#dat = read.table(text = tFile)
	dat <- data.frame ("RunT"=dat$V3,"Period"=dat$V4,"rStart"=dat$V7)
	
	return(dat)
}

pt <-loadData("C5/dyntick/2-1/rt-app-tst-20.log")

a = mean(pt$RunT)
b = max (pt$Period)
print(a)
print(b)
