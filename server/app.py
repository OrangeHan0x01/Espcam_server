#写一个例程，获取socket数据整合并保存图片、传输流到web
import camserver
import tcpserver
import threading
from flask import Flask,Response
import cv2
import numpy as np
#1、初始化picdict标准列表、cam列表和Socket服务器
picdict=camserver.Standard_Pics()
camlist=camserver.init_cam('./describe.txt')
skserver=tcpserver.Socket_pic()

app = Flask(__name__)

store_path=camserver.picstore_init('./')#创建保存路径

global enpic#全局变量，用于在线程间共享处理过的图片
global change_flag
change_flag=0
enpic=np.array([0,0,0,0])
#2、定义所用的socket回调函数，先getpics_socket处理获取数据frame和camid，再调用Standard_Pics.update进行更新，
#存储：直接用picstore在每张图片update同时存储。get后pic_process整合为整体图像并pic_showrt
def dfstream(data):
	global enpic
	global change_flag
	frame,dcamid=tcpserver.getpics_socket(data,camlist)
	print('[+]data-recived!')
	#except:
	#	print('error-happen in getpics_socket.')
	#	return 0
	picdict.update(dcamid,frame)
	pic_list,id_list=picdict.get()
	rt_flag=camserver.pic_store(pic_list,id_list,store_path)
	#print('[*]rtflag:',rt_flag)
	enpic=camserver.pic_process(pic_list)
	change_flag+=1
	if(change_flag>=80000):
		change_flag=1


def pic2rt():#最后需要返回yield数据
	global enpic
	global change_flag
	flag0=0
	#print('pic2rt,waiting.')
	while True:
		if change_flag!=flag0:#即enpic产生变化时
			try:
				print('flag_changed,start imencode',change_flag,'flag0:',flag0)
				ret, buffer = cv2.imencode('.jpg', enpic)
				frame = buffer.tobytes()
				flag0=change_flag
				yield (b'--frame\r\n'+b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')
			except:
				pass
		else:
			pass

#3、向Socket_pic()传入回调函数
def thread_run():
	skserver.start_listen(dfstream)
#这个函数应该用threading库作为一个线程启动，线程间是共享全局变量的
th = threading.Thread(target=thread_run, daemon=True)#当只有被设定为 daemon = true的线程存活时，整个程序结束。主线程不是守护线程，因此主线程创建的所有线程默认都是 daemon = False。
th.setDaemon(True)
th.start()#join可以让主线程阻塞等待至子线程结束


@app.route('/video_feed')#提供视频流，问题：此路由下方法能否与start_listen异步？用enpic能否实现异步？
def video_feed():#pic2rt
    try:
        return Response(pic2rt(), mimetype='multipart/x-mixed-replace; boundary=frame')
    except:
        return 'Can not return data!'


app.run()