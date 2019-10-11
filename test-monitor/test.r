# Set the working directory
setwd("./container/pretest/")
library(ggplot2)
library(reshape2)

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

test1 <-loadData("T3/test1/2-1/rt-app-tst-20.log")
test2 <-loadData("T3/test3/2-1/rt-app-tst-20.log")

test1$test <- "test1"
test2$test <- "test2"

# and combine into your new data frame vegLengths
vegLengths <- rbind(test1, test2)

# ggplot(vegLengths, aes(x=RunT, fill = test)) + 
#    geom_histogram(alpha = 0.5, aes(y = ..density..), position = 'identity')
ggplot(vegLengths, aes(x=RunT, fill = test)) + geom_density(alpha = 0.2)

# ggplot(datt, aes(x=counts, value, color=variable)) +
# 	#xlim (c(0,100)) + 
# 	ylab("Count") + 
# 	scale_x_continuous(trans='log10') +
# 	scale_y_continuous(trans='log10') +
#   geom_line()

