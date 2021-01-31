from Tkinter import * 
from bme280 import *

class ExampleApp(Frame):
    def __init__(self, master=None):
        Frame.__init__(self, master)
        self["bg"] = "white"
        self.pack()
        self.createWidgets()

    def createWidgets(self):
        self.w = 20
        self.w1 = 13
        self.h = 6    
        self.imgT = PhotoImage(file='/home/pi/my_ws/module_bme280/icons/temp1.png')
        self.imgP = PhotoImage(file='/home/pi/my_ws/module_bme280/icons/presureFancy.png')
        self.imgH = PhotoImage(file='/home/pi/my_ws/module_bme280/icons/Hum1.png')
        self.imgT = self.imgT.subsample(4)
        self.imgH = self.imgH.subsample(4)
        self.imgP = self.imgP.subsample(4) 
        self.labelTph = Label(self,  bg="white",image=self.imgT)
        self.labelHph = Label(self,  bg="white",image=self.imgH , borderwidth=1)
        self.labelPph = Label(self,  bg="white",image=self.imgP, borderwidth=1)
           

        self.labelT = Label(self, bg="white", text="Temperature:", height=self.h, width=self.w1, borderwidth=1)
        self.labelH = Label(self,  bg="white", text="   Humidity:", height=self.h, width=self.w1, borderwidth=1)
        self.labelP = Label(self,  bg="white", text="    Presure:", height=self.h, width=self.w1, borderwidth=1)
        self.labelT.config(font=("Lucida Grande", 18))
        self.labelH.config(font=("Lucida Grande", 18))
        self.labelP.config(font=("Lucida Grande", 18))

        self.labelFr = Label(self,  bg="white", text="Update Period:", height=1, width=self.w, borderwidth=1)
        self.labelFrV = Label(self,  bg="white", text="",              height=1, width=self.w, borderwidth=1)
        self.labelFr.config(font=("Lucida Grande", 12))
            
        self.labelTV = Label(self,  bg="white", text="", borderwidth=0, height=self.h, width=self.w, relief="groove")       
        self.labelHV = Label(self,  bg="white", text="", borderwidth=0, height=self.h, width=self.w, relief="groove") 
        self.labelPV = Label(self,  bg="white",  text="", borderwidth=0, height=self.h, width=self.w, relief="groove") 
        self.labelTV.config(font=("Lucida Grande", 18))
        self.labelHV.config(font=("Lucida Grande", 18))
        self.labelPV.config(font=("Lucida Grande", 18))

        self.labelTph.grid(column=0, row=0)
        self.labelHph.grid(column=0, row=2)
        self.labelPph.grid(column=0, row=1)
        
        self.labelT.grid(column=1, row=0)
        self.labelH.grid(column=1, row=2)
        self.labelP.grid(column=1, row=1)
        self.labelTV.grid(column=2, row=0)
        self.labelHV.grid(column=2, row=2)
        self.labelPV.grid(column=2, row=1)

        self.labelFr.grid(column=0, row=4)
        
        self.UpdateWeather()

    def UpdateWeather(self):
        period = 2000
        temperature,pressure,humidity = readBME280All()
        
        self.labelTV.configure(text="{} C".format(round(temperature,2)))
        self.labelHV.configure(text="{} %".format(round(humidity,2)))
        self.labelPV.configure(text="{} hPa".format(round(pressure,2)))
        period = readPeriod()
        self.labelFr.configure(text="Update each: {}s".format(int(period)/1000.0))

        self.after(int(period), self.UpdateWeather)
        

if __name__ == "__main__":
    root = Tk()
    root.title("Weather Widget")
    app = ExampleApp(master=root)
    app.mainloop()
    root.destroy()

