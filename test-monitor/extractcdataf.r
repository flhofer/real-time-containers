source("funcs.R")

# Set the working directory
setwd("./container/test/")
options("width"=200)
library(ggplot2)

tests  <- cbind( tests, list(group4)) # overwrite for now


#machines <- c("C5", "BM" , "T3", "T3U")
machines <- c("BM")

for (i in 1:length(machines)) {
	for (l in 1:length(tests)) {					

		tplot <- data.frame()

		for (k in 1:length(tests[[l]])) {
			testPcount = 0;


			# Label and directory pattern
			cat(machines[i], "-", tests[[l]][[k]], "\n")
			
			# Find all directories with pattern.. 
			dir = paste0(machines[i], "/", tests[[l]][[k]])

			maxAll = 0
			pcountAll = 0

			r<- data.frame (matrix(ncol=13,nrow=0))
			plot <- data.frame()
			names(r) <- c("Min", "Mdn", "Avg", "AvgDif","AvgDev", "Max", "pMin", "pMdn", "pAvg", "pavgDev", "pMax", "pOVPeak", "pcount")
			nr = 0
			files <- list.files(path=dir, pattern="*.log", full.names=TRUE, recursive=FALSE)

			# Load Container result file of this experiment, one file per container
			for (x in files) {

				dat <- loadData(x) # load log file of experiment, container
				#dat <- dat[-c(1:(10000000/dat$cDur)),] # delete first second

				datp <- dat[!(dat$RunT > dat$cDur*1.5),] # Dump all values higher than 1.5*  programmed duration (filter of overshoots for datp)

				minMin = min(datp$RunT)
				avgMea = mean(datp$RunT)
				avgMed = median(datp$RunT)
				avgDev = sqrt(var(datp$RunT))
				avgDif = avgMed-avgMea;
				maxMax = max(datp$RunT)
				pminMin = min(datp$Period)
				pavgMea = mean(datp$Period)
				pavgMdn = median(datp$Period)
				pavgDev = sqrt(var(datp$Period))
				pmaxMax = max(datp$Period)
				pmaxMaxp = max(dat$Period)
				pcount = sum ( dat$Period > pavgMea*1.5) # WARING! could loose some results!!
				maxAll = max(maxMax, maxAll)
				pcountAll = pcountAll + pcount

				r[nrow(r)+1,] <-data.frame (Min=minMin, Mdn=avgMed, Avg=avgMea, AvgDif=avgDif, AvgDev=avgDev, Max=maxMax, pMin=pminMin, pMdn=pavgMdn, pAvg=pavgMea, pAvgDev=pavgDev, pMax=pmaxMax, pOVPeak=pmaxMaxp, pcount)
				plot <- rbind(plot, dat)
			}
			plot$test <- tests[[l]][[k]]
			plot$oversh <- pcountAll

			datp <- plot[!(plot$RunT > plot$cDur*1.5),] # Dump all values higher than 1.5*  programmed duration (filter of overshoots for datp)

			minMin = min(datp$RunT)
			avgMea = mean(datp$RunT)
			avgMed = median(datp$RunT)
			avgDev = sqrt(var(datp$RunT))
			avgDif = avgMed-avgMea;
			maxMax = max(datp$RunT)
			pminMin = min(datp$Period)
			pavgMea = mean(datp$Period)
			pavgMdn = median(datp$Period)
			pavgDev = sqrt(var(datp$Period))
			pmaxMax = max(datp$Period)
			pmaxMaxp = max(plot$Period)
			pcount = sum (plot$Period > pavgMea*1.5) # WARING! could loose some results!!
			maxAll = max(maxMax, maxAll)

			rm <-data.frame (Min=minMin, Mdn=avgMed, Avg=avgMea, AvgDif=avgDif, AvgDev=avgDev, Max=maxMax, pMin=pminMin, pMdn=pavgMdn, pAvg=pavgMea, pAvgDev=pavgDev, pMax=pmaxMax, pOVPeak=pmaxMaxp, pcount)


			# Bind to total result
			tplot<-rbind(tplot,plot)
			print(r)
			cat ("totals:\n")
			print(rm)
			cat ("Peak - Peak count ", maxAll, pcountAll, "\n")
			testPcount = testPcount + pcountAll

		}

  		# plot single test group
  		pdf(file= paste0(machines[i],"_" , tests[[l]][[k]],".pdf"), width = 10, height = 10)

		tplot <- tplot[!((tplot$RunT > tplot$cDur*1.5)),] # drop exceeding points
  		hist <- ggplot(tplot, aes(x=test, y=RunT), col = rainbow(7)) + 
#			scale_y_continuous(limits=quantile(tplot$RunT, c(0.1,0.9))) +
		 	geom_boxplot(fill="slateblue", alpha=0.2) +
		    geom_point(aes(x = test, y = tplot$cDur*1.5, size=oversh), shape=17, , color="red", fill="red") +
		    labs(x="Test setup", y= expression(paste("Run-time  (values are in ", mu, "s)")), size=">10 ms in\n% of set")+
			scale_fill_viridis_d() 
  		print (hist)
  		dev.off()

		#		scale_y_continuous(trans='log10') +
  		# 		ylim (c(tplot$RunT[1]*0.995,tplot$RunT[1]*1.005)) + 
  		#		xlim (c(tplot$RunT[1]*0.995,tplot$RunT[1]*1.005)) + 
		#	    geom_point(data = tplot, aes(x = type, y = tplot$cDur*1.5, size=oversh), shape=17, , color="red", fill="red") +

        # head(tplot)
        # pdf(file= paste0(machines[i],"_" , tests[l,k],".pdf"), width = 10, height = 10)
        # hist <- ggplot(tplot, aes(x=RunT, fill = types), col = rainbow(7)) + 
        #         geom_density(alpha = 0.1) +
        #         xlim (c(tplot$cDur[1]*0.95,tplot$cDur[1]*1.05)) + 
        #         ylab("Count") +
        #         scale_fill_viridis_d()
        # print (hist)
        # dev.off()

		cat ("Test set overruns ", testPcount , "\n")
		cat ("----------------------------------------\n")
	}
}

