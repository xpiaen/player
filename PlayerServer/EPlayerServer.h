#pragma once
#include "CServer.h"
#include "Logger.h"
#include "HttpParser.h"
#include "Crypto.h"
#include "MysqlClient.h"
#include "jsoncpp/json.h"
#include <map>

//测试 http://192.168.1.111:9999/login?time=1649489265&salt=33250&user=test&sign=d112984d7ec209ce607202bb99e90252

DECLARE_TABLE_CLASS(user_login_mysql, _mysql_table_)
DECLARE_MYSQL_FIELD(TYPE_INT, user_id, NOT_NULL | PRIMARY_KEY | AUTOINCREMENT, "INTEGER", "", "", "")//用户ID
DECLARE_MYSQL_FIELD(TYPE_VARCHAR, user_qq, NOT_NULL, "VARCHAR", "(15)", "", "")  //QQ号
DECLARE_MYSQL_FIELD(TYPE_VARCHAR, user_phone, DEFAULT, "VARCHAR", "(11)", "18888888888", "") // 手机
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_name, NOT_NULL, "TEXT", "", "", "")  //姓名
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_nick, NOT_NULL, "TEXT", "", "", "")  //昵称
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_wechat, DEFAULT, "TEXT", "", "NULL", "")//微信号
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_wechat_id, DEFAULT, "TEXT", "", "NULL", "")//微信ID
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_address, DEFAULT, "TEXT", "", "", "")//地址
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_province, DEFAULT, "TEXT", "", "", "")//省份
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_country, DEFAULT, "TEXT", "", "", "")//国家
DECLARE_MYSQL_FIELD(TYPE_INT, user_age, DEFAULT | CHECK, "INTEGER", "", "18", "")//年龄
DECLARE_MYSQL_FIELD(TYPE_INT, user_male, DEFAULT, "BOOL", "", "1", "")//性别
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_flags, DEFAULT, "TEXT", "", "0", "")
DECLARE_MYSQL_FIELD(TYPE_REAL, user_experience, DEFAULT, "REAL", "", "0.0", "")
DECLARE_MYSQL_FIELD(TYPE_INT, user_level, DEFAULT | CHECK, "INTEGER", "", "0", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_class_priority, DEFAULT, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_REAL, user_time_per_viewer, DEFAULT, "REAL", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_career, NONE, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_password, NOT_NULL, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_INT, user_birthday, NONE, "DATETIME", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_describe, NONE, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_education, NONE, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_INT, user_register_time, DEFAULT, "DATETIME", "", "CURRENT_TIMESTAMP", "")
DECLARE_TABLE_CLASS_END()

/*
* 1.客户端的地址问题
* 2.连接回调的参数问题
* 3.接收回调的参数问题
*/

#define ERR_RETURN(ret,err)if(ret!=0){TRACEW("ret = %d errno = %d msg = [%s]", ret, errno, strerror(errno));return err;}
#define WARN_CONTINUE(ret)if(ret!=0){TRACEW("ret = %d errno = %d msg = [%s]", ret, errno, strerror(errno));continue;}

class CEPlayerServer : public CBusiness
{
public:
	CEPlayerServer(unsigned count) :CBusiness(){
		m_count = count;
	}
	~CEPlayerServer() {
		if (m_db) {
			CDataBaseClient* db = m_db;
			m_db = nullptr;
			db->Close();
			delete db;
		}
		m_epoll.Close();
		m_pool.Close();
		for (auto it : m_mapClients) {
			if (it.second!= nullptr) {
				delete it.second;
				it.second = nullptr;
			}
		}
		m_mapClients.clear();
	}
	virtual int BusinessProcess(CProcess* proc) 
	{
		using namespace std::placeholders;
		int ret = 0;
		m_db = new CMysqlClient();
		if (m_db == nullptr) {
			TRACEE("no more memory!");
			return -1;
		}
		KeyValue args;
		args["host"] = "192.168.1.111";
		args["port"] = 3306;
		args["user"] = "root";
		args["password"] = "123456";
		args["database"] = "edoyun";
		ret = m_db->Connect(args);
		ERR_RETURN(ret, -2);
		user_login_mysql user;
		ret = m_db->Exec(user.Create());
		ERR_RETURN(ret, -3);
		ret = setConnectedCallback(&CEPlayerServer::Connected, this, _1);
		ERR_RETURN(ret, -4);
		ret = setRecvCallback(&CEPlayerServer::Received, this, _1, _2);
		ERR_RETURN(ret, -5);
		ret = m_epoll.Create(m_count);
		ERR_RETURN(ret, -6);
		ret = m_pool.Start(m_count);
		ERR_RETURN(ret, -7);
		for (unsigned i = 0; i < m_count; i++) {
			ret = m_pool.AddTask(&CEPlayerServer::ThreadFunc, this);
			if (ret != 0)ERR_RETURN(ret, -8);
		}
		sockaddr_in addrin;
		int sock = 0;
		while (m_epoll != -1) {
			ret = proc->RecvSocket(sock, &addrin);
			if (ret < 0 || sock == 0)break;
			CSocketBase* pClient = new CSocket(sock);
			if (pClient == nullptr)continue;
			ret = pClient->Init(CSockParam(&addrin, SOCK_ISIP));
			WARN_CONTINUE(ret);
			ret = m_epoll.Add(sock, EpollData((void*)pClient));
			if (m_connectedcallback != nullptr) {
				(*m_connectedcallback)(pClient);
			}
			WARN_CONTINUE(ret);
		}
		return 0;
	}
private:
	int Connected(CSocketBase* pClient)
	{//TODO: 客户端连接处理 简单打印一下客户端的信息
		sockaddr_in* paddr = *pClient;
		TRACEI("client connected addr %s  port:%d", inet_ntoa(paddr->sin_addr), paddr->sin_port);
		return 0;
	}
	int Received(CSocketBase* pClient,const Buffer& data) 
	{   //TODO:主要业务处理
		//HTTP解析
		TRACEI("Received data!!!");
		int ret = 0;
		Buffer response = "";
		ret = HttpParser(data);
		TRACEI("HttpParser ret=%d", ret);
		//验证结果的反馈
		if (ret != 0) {//验证失败
			TRACEE("http parser failed!%d", ret);
		}
		response = MakeResponse(ret);
		ret = pClient->Send(response);
		if (ret != 0) {
			TRACEE("http responsesend failed!%d response:[%s]", ret, (char*)response);
		}
		else {
			TRACEE("http response send success!%d", ret);
		}
		return 0;
	}
	int HttpParser(const Buffer& data) {
		CHttpParser parser;
		printf("data:%s--------\n", data.c_str());
		size_t size = parser.Parser(data);
		if (size == 0 || (parser.Errno() != 0)) {
			TRACEE("size %llu errno:%u", size, parser.Errno());
			return -1;
		}
		int ret = 0;
		printf("=====================method:%s uri:%s=======================\n", parser.Method(), parser.Url());
		if (parser.Method() == HTTP_GET) {
			//get处理
			UrlParser url("https://192.168.1.111" + parser.Url());
			ret = url.Parser();
			if (ret != 0) {
				TRACEE("ret = %d url[%s", ret,"https://192.168.1.111"+parser.Url());
				return -2;
			}
			Buffer uri = url.Uri();
			if (uri == "login") {
				//登录请求处理
				Buffer time = url["time"];
				Buffer salt = url["salt"];
				Buffer user = url["user"];
				Buffer sign = url["sign"];
				TRACEI("time:%s salt:%s user:%s sign:%s", (char*)time, (char*)salt, (char*)user, (char*)sign);
				//TODO: 数据库查询
				user_login_mysql dbuser;
				if (m_db == nullptr)return -3;
				Result result;
				Buffer sql = dbuser.Query("user_name=\"" + user + "\"");
				ret = m_db->Exec(sql, result,dbuser);
				if (ret != 0) {
					TRACEE("sql=%s ret=%d", (char*)sql, ret);
					return -4;
				}
				if (result.size() == 0) {
					TRACEE("user not found!sql=%s ret=%d", (char*)sql, ret);
					return -5;
				}
				if (result.size() != 1) {
					TRACEE("more than one! sql=%s ret=%d", (char*)sql, ret);
					return -6;
				}
				auto user1 = result.front();
				Buffer pwd = *user1->Fields["user_password"]->Value.String;
				TRACEI("password = %s", (char*)pwd);
				//TODO: 登录验证
				const char* MD5_KEY = "*&^%$#@b.v+h-b*g/h@n!h#n$d^ssx,.kl<kl";
				Buffer md5str = time + MD5_KEY + pwd + salt;
				Buffer md5 = Crypto::MD5(md5str);
				TRACEI("md5 = %s", (char*)md5);
				if (md5 == sign) {
					return 0;
				}
				return -7;
			}
		} 
		else if (parser.Method() == HTTP_POST) {
			//post处理

		}
		return -8;
	}
	Buffer MakeResponse(int ret) {
		Json::Value root;
		root["status"] = ret;
		if (ret != 0) {
			root["message"] = "登录失败,请检查用户名和密码！";
		}
		else {
			root["message"] = "success";
		}
		Buffer json = root.toStyledString();
		Buffer result = "HTTP/1.1 200 OK\r\n";
		time_t t;
		time(&t);
		tm* ptm = localtime(&t);
		char temp[64] = "";
		strftime(temp, sizeof(temp), "%a, %d %b %G %T GMT\r\n", ptm);
		Buffer Date = Buffer("Date: ") + temp;
		Buffer Server = "Server: Edoyun/1.0\r\nContent-Type: text/html; charset=utf-8\r\nX-Frame-Options: DENY\r\n";
		snprintf(temp, sizeof(temp), "%d", json.size());
		Buffer Length = Buffer("Content-Length: ") + temp + "\r\n";
		Buffer Stub = "X-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\n\r\n";
		result += Date + Server + Length + Stub + json;
		// TRACEI("response: %s", (char *)result);
		return result;
	}
private:
	int ThreadFunc() {
		int ret = 0;
		EPEvents events;
		while ((m_epoll != -1)) {
			ssize_t size = m_epoll.WaitEvents(events);
			if (size < 0)break;
			if (size > 0) {
				for (ssize_t i = 0; i < size; i++) {
					if (events[i].events & EPOLLERR) {
						break;
					}
					else if (events[i].events & EPOLLIN) {
						CSocketBase* pClient = (CSocketBase*)events[i].data.ptr;
						if (pClient != nullptr) {
							Buffer data;
							ret = pClient->Recv(data);
							TRACEI("Received data size:%llu ", ret);
							if (ret <= 0) {
								TRACEW("ret = %d errno = %d msg = [%s]", ret, errno, strerror(errno)); 
								m_epoll.Del(*pClient);
								continue; 
							}
							if (m_recvcallback != nullptr) {
								(*m_recvcallback)(pClient,data);
								printf("Recv_Data:%s  size:%lu-------------\n", data.c_str(), data.size());
							}
						}
					}
				}
			}
		}
		return 0;
	}
private:
	CEpoll m_epoll;
	std::map<int, CSocketBase*> m_mapClients;
	CThreadPool m_pool;
	unsigned m_count;
	CDataBaseClient* m_db;
};