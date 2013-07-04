#! /usr/bin/python2.7 -OO
# -*- coding: utf-8 -*-
"""
Copyright 2013 Barnaby Shearer <b@Zi.iS>, Gary Fletcher <garygfletcher@hotmail.com>
License GPLv2
"""

import sys
from PyQt4 import QtCore, QtGui
import signal
import math
import random
import serial
import time
import binascii
import select

BAUD_RADIO = 115200
DEBUG = 1

class Display(QtGui.QWidget):

    _step = 0

    def __init__(self, parent = None):
        super(Display, self).__init__(parent)

        self._timer = QtCore.QBasicTimer()
        self._timer.start(100, self)

        self.setCursor(QtGui.QCursor(QtCore.Qt.BlankCursor))

        self._font = QtGui.QFont()
        self._font.setPointSize(30)
        self._font_metrics = QtGui.QFontMetrics(self._font)
        self.setFont(self._font)

        self._pic = {}
        self._newpic = {}
        self._text = {}
        self._oldpics = {}        

        self._oldpics[0] = [None, None, None, None, None, None, None, None]
        self._oldpics[1] = [None, None, None, None, None, None, None, None]

        self._newpic[0] = ''
        self._newpic[1] = ''

        self._text[0] = 'Loading 0...'
        self._text[1] = 'Loading 1...'

    def paintEvent(self, event):
       
        for i in [0, 1]:
            if self._newpic[i] != '':
                try:
                    self._oldpics[i].append(self._pic[i].scaled(QtCore.QSize(self.width()/8,self.height()/8), transformMode=QtCore.Qt.SmoothTransformation))
                    self._oldpics[i].pop(0)
                except KeyError:
                    pass
                if DEBUG > 0:
                    print "New Pic %d" % i
                qp = QtGui.QPixmap()
                qp.loadFromData(self._newpic[i], "JPG")
                self._pic[i] = qp.scaled(QtCore.QSize(self.width()/2,self.height()/2), transformMode=QtCore.Qt.SmoothTransformation)
                self._newpic[i] = ''
       
        painter = QtGui.QPainter(self)
        painter.setRenderHint(QtGui.QPainter.Antialiasing)

        painter.setBrush(QtGui.QColor([0,0,0]))
        painter.drawRect(QtCore.QRect(QtCore.QPoint(0,0), self.size()))

        painter.translate(self.width() / 2, self.height() / 2)
        scale = [200.0/self.height(), 200.0/self.height()]

        for p in [0,1]:
            try:
                painter.drawPixmap(QtCore.QPoint(-self.width()/2*(1-p),-self.height()/4), self._pic[p])
            except KeyError:
                pass

            for i in [0,1,2,3]:
                if self._oldpics[p][i]:
                    painter.drawPixmap(QtCore.QPoint(-self.width()/2*(1-p) + self.width()/8*i,-self.height()/16*6), self._oldpics[p][i])
    
            for i in [4,5,6,7]:
                if self._oldpics[p][i]:
                    painter.drawPixmap(QtCore.QPoint(-self.width()/2*(1-p) + self.width()/8*(i-4),+self.height()/4), self._oldpics[p][i])

        self.paintText(painter, u'rLab Conservation Camera using RaspberryPi + Ciseco RFµ', [0, -self.height()/16*7+(self._font_metrics.ascent() + self._font_metrics.descent())/2])

        self.paintText(painter, self._text[0], [-self.width()/4, self.height()/16*7+(self._font_metrics.ascent() + self._font_metrics.descent())/2])
        self.paintText(painter, self._text[1], [self.width()/4, self.height()/16*7+(self._font_metrics.ascent() + self._font_metrics.descent())/2])

    def paintText(self, painter, text, offset):
        x = (-self._font_metrics.width(text)) / 2 + offset[0]
        y = (-self._font_metrics.ascent() - self._font_metrics.descent()) / 2 + offset[1]

        index = self._step
        for letter in text:
            color = QtGui.QColor()
            color.setHsv(255 - (index * 16) % 255, 255, 191)
            painter.setPen(color)
            painter.drawText(
                x,
                y - (math.sin(index * math.pi/8) * self._font_metrics.height() / 8),
                letter
            )
            x += self._font_metrics.width(letter)
            index += 1

    def timerEvent(self, event):
        if event.timerId() == self._timer.timerId():
            self._step += 1
            self.update()
        else:
            super(Display, self).timerEvent(event)

app = QtGui.QApplication(sys.argv)
display = Display()

class SerialThread(QtCore.QThread):
    def __init__(self):
        QtCore.QThread.__init__(self)

    def run(self):
        display._text[0] = "Ready"
        display._text[1] = "Ready"
        s = serial.Serial('/dev/ttyAMA0', BAUD_RADIO, timeout = .01)
        data = [None, None]
        length = [None, None]
        while True:
            for node in [0, 1]:
                #Select Node
                s.flush()
                s.flushInput()
                #Enquire
                if DEBUG > 1:
                    print "\n====== ENQUIRE %d =====" % node
                s.write(''.join([chr(5),chr(node)]*6))
                s.flush()
                slot = time.time() + .5
                buf = [0] * 12
                while time.time() < slot:
                    if select.select([s],[],[], max(0,slot - time.time()))[0] != []:
                        buf.pop(0)
                        buf.append(s.read(1))
                        if DEBUG > 2:
                            print buf[-1],
                        if buf == ['\x04'] * 12:
                            if data[node]:
                                data[node] = data[node][:-11]
                            if DEBUG > 1:
                                print "\nEOT"
                            break
                        if buf == ['\x06'] * 12:
                            slot += 2
                            if data[node]:
                                data[node] = data[node][:-11]
                            if DEBUG > 1:
                                print "\nACK"
                            continue
                        if data[node] != None:
                            if buf[0:3] == ['C','H','K'] and buf[11] == '\x17':
                                display._text[node] = 'Sleeping'
                                try:
                                    if binascii.crc32(data[node][:length[node]]) & 0xffffffff == int(''.join(buf[3:11]),16):
                                        display._newpic[node] = data[node][:length[node]]
                                    else:
                                        print "!!!!!!! Checksum missmatch !!!!!"
                                    if DEBUG > 0:
                                        print "Checksum:", hex(binascii.crc32(data[node][:length[node]]) & 0xffffffff), ''.join(buf)
                                except:
                                    pass
                                data[node] = None
                            else:
                                data[node] += buf[-1]
                                if len(data[node]) > length[node] + 200:
                                    data[node] = None
                                    if DEBUG > 0:
                                        print "\nOVER LENGTH"
                                    break
                                display._text[node] = "%d%% (%d bytes)" % (
                                    (100.0 * len(data[node]) / length[node]),
                                    length[node]
                                )
                        else:
                            if buf[0:6] == ['\x01','N','E','W','_','P'] and buf[11] == '\x02':
                                if DEBUG > 0:
                                    print "NewPic"
                                try:
                                    if int(''.join(buf[6:11]))>0:
                                        data[node] = ''
                                        length[node] = int(''.join(buf[6:11]))
                                except:
                                    pass
                            else:
                                if buf[-1] == '\r':
                                    if DEBUG > 0 and DEBUG < 3:
                                        print
                                else:
                                    if ord(buf[-1]) > 20:
                                        if DEBUG > 0 and DEBUG < 3:
                                            sys.stdout.write(buf[-1])

def main(argv):
    s = SerialThread()
    s.start()
    display.show()
    return app.exec_()

if __name__ == '__main__':
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    main(sys.argv)
