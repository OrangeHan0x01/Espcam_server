import cv2
import numpy as np
import time
import os

#esp32cam建议使用FRAMESIZE_QVGA (320 x 240)或者FRAMESIZE_VGA (640 x 480)，jpeg_quality值越大则质量越低（同时降低图片大小）

def init_cam(setfile):#读取描述文件，获取id，类型（web或者socket），名称，静态ip，位置和备注，以[{字典}]格式保存返回，这些数据也会保存一个备份到存储数据的文件夹
	cam_list=[]
	with open(setfile) as f:
		list1=f.readlines()
		for data0 in list1:
			init0=data0.split(',')
			cam={'id':init0[0],'class':init0[1],'name':init0[2],'ip':init0[3],'pos':init0[4],'remark':init0[5].strip()}
			cam_list.append(cam)
	return cam_list

def get_pics(cam_list,imgsize=(480,640,3),overtime=0.3,ret_empty=0):#用for获取images并返回一个images列表,ret_empty指定在超时时是否返回空图像，1打开
	timenow=time.time()
	img_list=[]
	id_list=[]#方便接下来处理，同时避免出现连接不上导致图片不正常
	for cam in cam_list:#摄像头需要返回multipart/x-mixed-replace; boundary=frame的数据
		ret=False
		time_temp=time.time()
		empty_img=False
		while not ret:
			ret, frame = cv2.VideoCapture('http://'+cam['ip']+'/').read()
			if(time.time()-time_temp>=overtime):
				ret=True
				frame=cv2.cvtColor(np.uint8(np.zeros(imgsize,dtype=bool)),cv2.COLOR_RGB2BGR)
				empty_img=True
		#frame.release()
		cv2.destroyAllWindows()#释放连接，必须
		timestp = time.localtime()
		time_now = time.strftime("%Y-%m-%d-%H_%M_%S", timestp)
		cv2.putText(frame,cam['id'],(5,15),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
		cv2.putText(frame,cam['name'],(5,30),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
		cv2.putText(frame,cam['pos'],(5,45),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
		cv2.putText(frame,time_now,(5,60),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
		if(empty_img and ret_empty):
			cv2.putText(frame,'----No Signal----',(5,75),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
		img_list.append(frame)
		id_list.append(cam['id'])
	timecast=time.time()-timenow
	return id_list,img_list,timecast#后期考虑：1、超时图像设置flag 2、把每个cam的等待时间作为列表返回，用来进行后续分析和处理 3、可选择超时时显示上一张图片还是空图像 4、可选择写入什么标志

def pic_process(img_list,imgsize=(480,640,3),auto_size=1):#第二个参数是原图像大小，用于生成空白图像，如果auto_size了就不用填
#图片边界：4，9，16，25，36...；4及以下每行2张，9及以下每行3张，依次类推
#合并：res = np.hstack([img1, img2])即可
	new_shape=[]#是数组！
	if(len(img_list)==0):
		return False
	if(auto_size):
		imgsize=np.shape(img_list[0])
		print('imgsize:',imgsize)
	for i in range(6):#
		if((len(img_list)>(i-1)*(i-1))&(len(img_list)<=i*i)):#不能用np.reshape...
			#print('concate:i=',i)
			j=0#行数
			for h in range(i):#这是行信息
				hd=[]
				for l in range(i):
					if((j*i+l)>=len(img_list)):#应该是这样不用改，但可以先用普通数组测试,不行，数目要跟上一致
						if(l!=0):#填充
							hd.append(np.zeros(imgsize,dtype=bool).tolist())#添加空白图片
							continue
						else:
							break#直接退出这行
					hd.append(img_list[j*i+l])
				if(len(hd)!=0):
					himg=np.hstack(hd)
					new_shape.append(himg)
				j+=1
			respic=np.vstack(new_shape)
			break
	print('img_shape:',np.shape(respic))
	return respic

def pic_zip(img,newsize=(640, 480)):#似乎是没有转换通道的功能的,w,h
	return cv2.resize(img,newsize)

def picstore_init(root_path):#在root_path下创建save_path,save_path根据时间戳创建
	timestp = time.localtime()
	time_now = time.strftime("%Y-%m-%d-%H_%M_%S", timestp)
	root_path=root_path.strip('/')#root_path输入类似：‘./文件夹名称’
	folder = root_path + '/' + time_now
	if not os.path.exists(folder):
		os.mkdir(folder)
	return folder#返回保存路径

#picstore_init的folder可以传入store_path
def pic_store(img_list,id_list,store_path,gray=False):#视频流存储，需要对文件编号和文件夹编号（需要开始日期时间）,和处理分开主要是为了单摄像头图片也能单独存储，这样方便了后期用ai识别处理
	strtime=str(time.time())#分段使用时，每次启动先创建好store_path的文件夹和拼接路径
	if(len(img_list)!=len(id_list)):
		raise ValueError('pic_store error:len(img_list)!=len(id_list)')
	if(gray):
		for i in range(len(id_list)):
			gray_img = cv2.cvtColor(img_list[i], cv2.COLOR_BGR2GRAY)
			cv2.imwrite(store_path+'/'+strtime+'_'+str(id_list[i])+'.jpg', gray_img)
	else:
		for i in range(len(id_list)):
			cv2.imwrite(store_path+'/'+strtime+'_'+str(id_list[i])+'.jpg', img_list[i])
	return 'success_save'

def pic2video(img_path,save_path,movsize=(640,480),fps=1):#图片转视频,需要使用文件夹避免内存不够，此外目前还没有区分单通道和三通道,w,h
	video = cv2.VideoWriter(save_path, cv2.VideoWriter_fourcc('m', 'p', '4', 'v'), fps, movsize)
	filelist = os.listdir(img_path.strip('/'))
	filelist.sort()
	for item in filelist:
		if item.endswith('.jpg'):
			item = img_path.strip('/')+'/' + item
			img = cv2.imread(item)
			video.write(img)
	video.release()
	cv2.destroyAllWindows()
	return 'success_videosave'


def pic_showrt(cam_list,store_path,zip_size=(640, 480),imgsize=(480,640,3)):#实时展示，相当于gen函数，需要整合前面的函数
	while True:
		id_list,img_list,timecast=get_pics(cam_list)
		print('read_imgs cost:',timecast)#timecast过大时应该需要退出循环，未写！
		zipimg_list=[]
		for img in img_list:
			zipimg_list.append(pic_zip(img,zip_size))
		respic=pic_process(zipimg_list,imgsize)
		ret, buffer = cv2.imencode('.jpg', respic)
		frame = buffer.tobytes()
		yield (b'--frame\r\n'+b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')


def showrt(respic):#精简版本，只需要图片,可能要改用全局变量
	ret, buffer = cv2.imencode('.jpg', respic)
	frame = buffer.tobytes()
	yield (b'--frame\r\n'+b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')


#video_capture = cv2.VideoCapture(video_path)
def pic_showv(video_capture):#视频流展示,如果重新获取这个页面，则从头开始演示视频
	video_capture = cv2.VideoCapture(video_capture)
	while True:
		isTrue, frame = video_capture.read()
		if not isTrue:
			break
		ret, buffer = cv2.imencode('.jpg', frame)
		frame = buffer.tobytes()
		yield (b'--frame\r\n'+b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')

def bytes2pic(jpeg_bytes):#字节流转图片
	frame = cv2.imdecode(np.frombuffer(jpeg_bytes,np.uint8), cv2.IMREAD_COLOR)
	#cv2.imshow("dfsdf",frame)
	#pil读取jpeg:buf = StringIO.StringIO()# 缓存对象;pi.save(buf, format='JPEG')# 将PIL下的图像压缩成jpeg格式，存入buf中;jpeg = buf.getvalue()# 从buf中读出jpeg格式的图像
	return frame
'''
	可能方法2
	jpeg = msg.replace("\-n", "\n")
	buf = StringIO.StringIO(jpeg[0:-1])# 缓存数据
	buf.seek(0)
	pi = Image.open(buf)# 使用PIL读取jpeg图像数据
	# img = np.zeros((640, 480, 3), np.uint8)#！！！！uint8可以尝试换成uchar
	img = cv2.cvtColor(np.asarray(pi), cv2.COLOR_RGB2BGR)# 从PIL的图像转成opencv支持的图像
'''
class Standard_Pics:
	def __init__(self):
		self.standard_imgdict={}#上一次的list,需要先添加时间戳等数据。｛'id':pic｝
	def init(self,camlist,imgsize=(480,640,3)):#创建一个标准化的起始图片列表，全是空图
		timestp = time.localtime()
		time_now = time.strftime("%Y-%m-%d-%H_%M_%S", timestp)
		for cam in camlist:
			frame=cv2.cvtColor(np.uint8(np.zeros(imgsize,dtype=bool)),cv2.COLOR_RGB2BGR)
			cv2.putText(frame,cam['id'],(5,15),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
			cv2.putText(frame,cam['name'],(5,30),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
			cv2.putText(frame,cam['pos'],(5,45),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
			cv2.putText(frame,time_now,(5,60),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
			cv2.putText(frame,'----Inited----',(5,75),cv2.FONT_HERSHEY_SIMPLEX,0.5,(0,0,255),1)
			self.standard_imgdict[cam['id']]=frame#使用cam的id作为idkey,初始化
	def update(self,camid,imgdata):#输入序号和图片，更新standard_imgdict
		#标准化思路：预配置一个图像字典，实时更新，找不到就用空图像代替。标准化不需要修改图片合并逻辑，只要在适当时候更新图片字典,然后把字典输入进去即可。
		self.standard_imgdict[camid]=imgdata#在执行这步操作之前
	def get(self):#获取用于拼接图像的图像列表
		retlist=[]
		idlist=list(self.standard_imgdict.keys())
		for idkey in idlist:
			retlist.append(self.standard_imgdict[idkey])
		return retlist,idlist


