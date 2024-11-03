#pragma once
#include "Public.h"
#include <map>
#include <list>
#include <memory>
#include <vector>

class _Table_;
using PTable = std::shared_ptr<_Table_>;

using KeyValue = std::map<Buffer, Buffer>;
using Result = std::list<PTable>;


class CDataBaseClient
{
public:
	CDataBaseClient(const CDataBaseClient&) = delete;
	CDataBaseClient& operator=(const CDataBaseClient&) = delete;
public:
	CDataBaseClient() {}
	virtual ~CDataBaseClient() {}
public:
	//连接
	virtual int Connect(const KeyValue& args) = 0;
	//执行
	virtual int Exec(const Buffer& sql) = 0;
	//带结果的执行
	virtual int Exec(const Buffer& sql, Result& result, const _Table_& table) = 0;
	//开启事务
	virtual int StartTransaction() = 0;
	//提交事务
	virtual int CommitTransation() = 0;
	//回滚事务
	virtual int RollbackTransaction() = 0;
	//关闭连接
	virtual int Close() = 0;
	//是否连接
	virtual bool IsConnected() = 0;
};

//表和列的基类的实现
class _Field_;
using PField = std::shared_ptr<_Field_>;
using FieldArray = std::vector<PField>;
using FieldMap = std::map<Buffer, PField>;

class _Table_ 
{
public:
	_Table_() {}
	virtual ~_Table_() {}
	//返回创建的SQL语句
	virtual Buffer Create() = 0;
	//删除表
	virtual Buffer Drop() = 0;
	//增删改查
	virtual Buffer Insert(const _Table_& values) = 0;//TODO:参数进行优化
	virtual Buffer Modify(const _Table_& values) = 0;//TODO:参数进行优化
	virtual Buffer Delete(const _Table_& values) = 0;
	virtual Buffer Query(const Buffer& condition = "") = 0;
	//创建一个基于表的对象
	virtual PTable Copy() const = 0;
	virtual void ClearFieldUsed() = 0;
public:
	//获取表的全名
	virtual operator const Buffer() const = 0;
public:
	//表所属的DB的名称
	Buffer DataBase;
	Buffer Name;
	FieldArray FieldDefine;//存放所有列指针的vector
	FieldMap  Fields;//记录列名与列指针的映射
};

enum {
	SQL_INSERT = 1,//插入的列
	SQL_MODIFY = 2,//修改的列
	SQL_CONDITION = 4//条件语句
};

enum {
	NONE = 0,
	NOT_NULL = 1,//非空
	DEFAULT = 2,//默认值
	UNIQUE = 4,//唯一
	PRIMARY_KEY = 8,//主键
	CHECK = 16,//检查约束
	AUTOINCREMENT = 32//自增
};

using SqlType = enum {
	TYPE_NULL = 0,
	TYPE_BOOL = 1,
	TYPE_INT = 2,
	TYPE_DATETIME = 4,
	TYPE_REAL = 8,
	TYPE_VARCHAR = 16,
	TYPE_TEXT = 32,
	TYPE_BLOB = 64
};

class _Field_
{
public:
	_Field_() {}
	_Field_(const _Field_& field)
	{
		Name = field.Name;
		Type = field.Type;
		Attr = field.Attr;
		Default = field.Default;
		Check = field.Check;
	}
	virtual _Field_ & operator=(const _Field_& field)
	{
		if (this != &field) {
			Name = field.Name;
			Type = field.Type;
			Attr = field.Attr;
			Default = field.Default;
			Check = field.Check;
		}
		return *this;
	}
	virtual ~_Field_() {}
public:
	virtual Buffer Create() = 0;
	virtual void LoadFromStr(const Buffer& str) = 0;
	//where 语句使用的
	virtual Buffer toEqualExp() const = 0;
	virtual Buffer toSqlStr() const = 0;
	//列的全名
	virtual operator const Buffer() const = 0;
public:
	Buffer Name;
	Buffer Type;
	Buffer Size;
	unsigned Attr;
	Buffer Default;
	Buffer Check;
public:
	//操作条件
	unsigned Condition;
	union {
		bool Bool;
		int Integer;
		double Double;
		Buffer* String;
	}Value;
	unsigned nType;
};


