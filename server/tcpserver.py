#需要：创建异步套接字，可以接收多个连接请求和接收数据
#需要一个字典，将‘简短id标识符’映射到服务器所用的cam_id
import cv2
import socket
import time
import camserver


def sid2camid(cam_list,sid):#将简短、用于传输的sid转换为cam_id并返回cam变量
	print('sid:',sid)
	for cam in cam_list:
		if(cam['id'][-6:].encode('ascii')==sid):#[-6:]取最后6字节
			#print('sid-finded!')
			return cam
	return 0#没有对应cam

def getpics_socket(data,cam_list,imgsize=(240,320,3),id_len=6):#从tcp套接字获取图片字节流和标识号并分割，默认其中前6个字节用作标识号（规范化，使lora等能共用地址-至少是其中的一部分）
	print('data_len:',len(data))
	cam_sid=data[:6]#前6字节
	picdata=data[6:]#第7字节开始为picdata
	print('data_head20:',picdata[:20])
	timestp = time.localtime()
	time_now = time.strftime("%Y-%m-%d-%H_%M_%S", timestp)
	cam=sid2camid(cam_list,cam_sid)#cam_sid解析为cam_id操作(就是看cam_id最后5字节，如一样就改为cam_id，否则直接用sid就好
	if(not cam):
		return 0,0
	frame=[0]
	frame=camserver.bytes2pic(picdata)#图片数据转图片操作
	if(len(frame)==0):
		return 0,cam['id']#没有返回值，因此这里图片返回0，主函数中注意不进行update
	cv2.putText(frame,cam['id'],(5,15),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
	cv2.putText(frame,cam['name'],(5,30),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
	cv2.putText(frame,cam['pos'],(5,45),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
	cv2.putText(frame,time_now,(5,60),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
	return frame,cam['id']#与web的get_pics不同，这里返回的是单张图片和对应摄像头

def tcp_dataprint(data):#打印tp数据
	print('data:',data)
	return 1

#最好是：一个nrf绑定4个cam模块,然后通过esp01s连接服务器，其中cam模块应该限制到3-4s传输一次
class Socket_pic:#总需要：1、不断接收数据，根据addr/前5字节绑定到cam_id，收到数据后立刻处理；这里可以分开（缓冲区），也可以一起处理
	def __init__(self,host='0.0.0.0',port=21572,bufsiz=35000):
		self.HOST = host  # HOST 变量是空白的，表示它可以使用任何可用的地址。
		self.PORT = port
		self.BUFSIZ = bufsiz#示例中其实是1024
		self.ADDR = (self.HOST, self.PORT)
		self.data=[]
		self.accept_stat=0
	def start_listen(self,callback_f):
		tcpSerSock = socket.socket(family=socket.AF_INET, type=socket.SOCK_STREAM)
		tcpSerSock.bind(self.ADDR)
		tcpSerSock.listen(5)  # 开始TCP监听,监听5个请求
		while True:#每接收到一个连接就开启一次；
			print("waiting for next connection...")
			tcpCliSock,addr = tcpSerSock.accept()#阻塞直到发生连接
			self.accept_stat=1
			print(f"... connected from: {addr}")
			st=time.time()
			tcast=time.time()
			flag_start=0
			while not tcpCliSock._closed:
				if not flag_start:
					data_all=tcpCliSock.recv(self.BUFSIZ)
					st=time.time()
					flag_start=1
				else:
					data=tcpCliSock.recv(self.BUFSIZ)
					data_all += data
					if(len(data)>=100):
						st=time.time()
					data=''
				if (time.time()-st)>=0.2:#200毫秒等待时间，目前不支持并发同时收听
					print('[-]time_out!')
					tcpCliSock.close()
					break
			print('time_cast:',time.time()-tcast)
			callback_f(data_all)#用回调函数进行处理，如果是摄像头数据，这个回调函数应该是一个整合函数（包括传入camlist的getpics和对有cam对象、处理过的图片的后续利用/存储）
			self.accept_stat=0
	def get_stat(self):
		return self.accept_stat










