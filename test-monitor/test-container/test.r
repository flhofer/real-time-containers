source("funcs.r")

# Set the working directory
setwd("./test/")
options("width"=200)
library(ggplot2)

tests <- list()
group1 <- paste0(c("1-"), 1:10)
group2 <- paste0(c("2-"), 1:2)
group3 <- paste0(c("3-"), 1:3)
group4 <- paste0(c("4-"), 1:10)
tests  <- cbind( tests, list(group1, group2, group3, group4))

machines <- c("C5", "BM" , "T3", "T3U")
#tests  <- cbind(list(group4)) # overwrite for now
#machines <- c("BMB", "T3B", "T3UB", "C5C") # overwrite for now, defaults to VM instance comparison T3/C5/BM
#machines <- c("test0","test1","test2","test3","test4","test5","test6","test7","test8","test9","test10","test11") # For kernel parameter comparison

sink("containerstats.txt")

for (i in 1:length(machines)) {
	for (l in 1:length(tests)) {					

		tplot <- data.frame()

		testPcount = 0;
		for (k in 1:length(tests[[l]])) {

			# Label and directory pattern
			cat(machines[i], "-", tests[[l]][[k]], "\n")
			
			# Find all directories with pattern.. 
			dir = paste0(machines[i], "/", tests[[l]][[k]])

			r<- data.frame()
			plot <- data.frame()
			files <- list.files(path=dir, pattern="*.log", full.names=TRUE, recursive=FALSE)

			# Load Container result file of this experiment, one file per container
			for (x in files) {

				dat <- loadData(x) # load log file of experiment, container
				if (nrow(dat) >0){
					dat <- dat[-c(1:(10000000/dat$cDur)),] # delete first second
					dat <- head(dat, -1) # delete last row as it is often incomplete
				}

				r <-rbind(r, getDataPars(dat))
				plot <- rbind(plot, dat)
			}
			pcountAll = sum(r$pcount)
			testPcount = testPcount + pcountAll

			# get overall statistics on all containers			
			plot$test <- tests[[l]][[k]]
			plot$oversh <- pcountAll
			rm <- getDataPars(plot)

			print(r)
			cat ("totals:\n")
			print(rm)

			# Bind to total result
			tplot<-rbind(tplot,plot)
		}

  		# plot single test group
  		pdf(file= paste0(machines[i],"_" , tests[[l]][[k]],".pdf"), width = 10, height = 10)

		tplot <- tplot[!((tplot$RunT > tplot$cDur*1.2)),] # drop exceeding points
	#	tplot <- tplot[!duplicated(tplot),] # drop duplicates
		
  		hist <- ggplot(tplot, aes(x=test, y=RunT), col = rainbow(7)) + 
			# scale_y_continuous(limits=quantile(tplot$RunT, c(0.1,0.9))) +
		 	geom_boxplot(fill="slateblue", alpha=0.2) +
		    geom_point(aes(x = test, y = tplot$cDur*1.2, size=oversh), shape=17, , color="red", fill="red") +
		    labs(x="Test setup", y= expression(paste("Run-time  (values are in ", mu, "s)")), size=">10 ms in\n% of set")+
			scale_fill_viridis_d() 
  		print (hist)
  		dev.off()

		#		scale_y_continuous(trans='log10') +
  		# 		ylim (c(tplot$RunT[1]*0.995,tplot$RunT[1]*1.005)) + 
  		#		xlim (c(tplot$RunT[1]*0.995,tplot$RunT[1]*1.005)) + 
		#	    geom_point(data = tplot, aes(x = type, y = tplot$cDur*1.5, size=oversh), shape=17, , color="red", fill="red") +

        head(tplot)
        pdf(file= paste0(machines[i],"_" , tests[[l]][[k]],"_h.pdf"), width = 10, height = 10)
        hist <- ggplot(tplot, aes(x=RunT, fill = test), col = rainbow(7)) + 
                geom_density(alpha = 0.1) +
                xlim (c(tplot$cDur[1]*0.95,tplot$cDur[1]*1.05)) + 
                ylab("Count") +
                scale_fill_viridis_d()
        print (hist)
        dev.off()

		cat ("Test set overruns ", testPcount , "\n")
		cat ("----------------------------------------\n")
	}
}

