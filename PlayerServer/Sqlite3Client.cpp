#include "Sqlite3Client.h"
#include "Logger.h"

int CSqlite3Client::Connect(const KeyValue& args)
{
	auto it = args.find("host");
	if (it == args.end())return -1;
	if (m_db != nullptr)return -2;
	int ret = sqlite3_open(it->second, &m_db);
	if (ret != SQLITE_OK) {
		TRACEE("connect failed:%d [%s]", ret, sqlite3_errmsg(m_db));
		return -3;
	}
	return 0;
}

int CSqlite3Client::Exec(const Buffer& sql)
{
	printf("sql={%s}\n", (const char*)sql);
	if (m_db == nullptr)return -1;
	int ret = sqlite3_exec(m_db, sql, nullptr,this, nullptr);
	if (ret != SQLITE_OK) {
		TRACEE("sql={%s}", sql);
		TRACEE("Exec failed:%d [%s]", ret, sqlite3_errmsg(m_db));
		return -2;
	}
	return 0;
}

int CSqlite3Client::Exec(const Buffer& sql, Result& result, const _Table_& table)
{
	char* errmsg = nullptr;
	if (m_db == nullptr)return -1;
	printf("sql={%s}\n", (const char*)sql);
	ExecParam param(this, result, table);
	int ret = sqlite3_exec(m_db, sql, &CSqlite3Client::ExecCallback, (void*)&param, &errmsg);
	if (ret != SQLITE_OK) {
		TRACEE("sql={%s}", sql);
		TRACEE("Query failed:%d [%s]", ret, errmsg);
		if (errmsg)sqlite3_free(errmsg);
		return -2;
	}
	if (errmsg)sqlite3_free(errmsg);
	return 0;
}

int CSqlite3Client::StartTransaction()
{
	if (m_db == nullptr)return -1;
	int ret = sqlite3_exec(m_db, "BEGIN TRANSACTION", 0, 0, nullptr);
	if (ret != SQLITE_OK) {
		TRACEE("sql={BEGIN TRANSACTION}");
		TRACEE("BEGIN failed:%d [%s]", ret, sqlite3_errmsg(m_db));
		return -2;
	}
	return 0;
}

int CSqlite3Client::CommitTransation()
{
	if (m_db == nullptr)return -1;
	int ret = sqlite3_exec(m_db, "COMMIT TRANSACTION", 0, 0, nullptr);
	if (ret != SQLITE_OK) {
		TRACEE("sql={COMMIT TRANSACTION}");
		TRACEE("COMMIT failed:%d [%s]", ret, sqlite3_errmsg(m_db));
		return -2;
	}
	return 0;
}

int CSqlite3Client::RollbackTransaction()
{
	if (m_db == nullptr)return -1;
	int ret = sqlite3_exec(m_db, "ROLLBACK TRANSACTION", 0, 0, nullptr);
	if (ret != SQLITE_OK) {
		TRACEE("sql={ROLLBACK TRANSACTION}");
		TRACEE("ROLLBACK failed:%d [%s]", ret, sqlite3_errmsg(m_db));
		return -2;
	}
	return 0;
}

int CSqlite3Client::Close()
{
	if (m_db == nullptr)return -1;
	int ret = sqlite3_close(m_db);
	if (ret != SQLITE_OK) {
		TRACEE("Close failed:%d [%s]", ret, sqlite3_errmsg(m_db));
		return -2;
	}
	m_db = nullptr;
	return 0;
}

bool CSqlite3Client::IsConnected()
{
	return m_db != nullptr;
}

int CSqlite3Client::ExecCallback(void* arg, int count, char** values, char** names)
{
	ExecParam* param = (ExecParam*)arg;
	return param->obj->ExecCallback(param->result, param->table, count, names, values);;
}

int CSqlite3Client::ExecCallback(Result& result, const _Table_& table, int count, char** names, char** values)
{
	PTable pTable = table.Copy();
	if (pTable == nullptr) {
		TRACEE("table %s error!", (const char*)(Buffer)table);
		return -1;
	}
	for (int i = 0; i < count; i++) {
		Buffer name = names[i];
		auto it = pTable->Fields.find(name);
		if (it == pTable->Fields.end()) {
			TRACEE("table %s error!", (const char*)(Buffer)table);
			return -2;
		}
		if (values[i] != nullptr)
			it->second->LoadFromStr(values[i]);
	}
	result.push_back(pTable);
	return 0;
}

_sqlite3_table_::_sqlite3_table_(const _sqlite3_table_& table)
{
	DataBase = table.DataBase;
	Name = table.Name;
	for (size_t i = 0; i < table.FieldDefine.size(); i++)
	{
		PField field = PField(new _sqlite3_field_(*(_sqlite3_field_*)table.FieldDefine[i].get()));
		FieldDefine.push_back(field);
		Fields[field->Name] = field;
	}

}

_sqlite3_table_::~_sqlite3_table_()
{}

Buffer _sqlite3_table_::Create()
{//CREATE TABLE IF NOT EXISTS 表全名(字段定义，主键定义，索引定义);
	//表全名 = 数据库名.表名
	Buffer sql = "CREATE TABLE IF NOT EXISTS " + (Buffer)*this + "(\r\n";
	for (size_t i = 0; i < FieldDefine.size(); i++) {
		if(i>0)sql+=",";
		sql += FieldDefine[i]->Create();
	}
	sql += ");";
	TRACEI("sql=%s", (const char*)sql);
	return sql;
}

Buffer _sqlite3_table_::Drop()
{
	Buffer sql = "DROP TABLE IF EXISTS " + (Buffer)*this + ";";
	TRACEI("sql=%s", (const char*)sql);
	return sql;
}

Buffer _sqlite3_table_::Insert(const _Table_& values)
{//INSERT INTO 表全名(字段1，字段2，...) VALUES(值1，值2，...);
	Buffer sql = "INSERT INTO " + (Buffer)*this + " (";
	bool isFirst = true;
	for (size_t i = 0; i < values.FieldDefine.size(); i++) {
		if (values.FieldDefine[i]->Condition & SQL_INSERT) {
			if (!isFirst)sql += ",";
			else isFirst = false;
			sql += (Buffer)*values.FieldDefine[i];
		}
	}
	sql += ") VALUES (";
	isFirst = true;
	for (size_t i = 0; i < values.FieldDefine.size(); i++) {
		if (values.FieldDefine[i]->Condition &  SQL_INSERT) {
			if (!isFirst)sql += ",";
			else isFirst = false;
			sql += values.FieldDefine[i]->toSqlStr();
		}
	}
	sql += ");";
	TRACEI("sql=%s", (const char*)sql);
	return sql;
}

Buffer _sqlite3_table_::Modify(const _Table_& values)
{//UPDATE 表全名 SET 字段=值 WHERE 条件;（条件可选）
	Buffer sql = "UPDATE " + (Buffer)*this;
	Buffer Set = "";
	bool isFirst = true;
	for (size_t i = 0; i < values.FieldDefine.size(); i++) {
		if (values.FieldDefine[i]->Condition & SQL_MODIFY) {
			if (!isFirst)Set += ",";
			else isFirst = false;
			Set += (Buffer)*values.FieldDefine[i] + " = " + values.FieldDefine[i]->toSqlStr();
		}
	}
	if (Set.size() > 0) {
		sql += " SET " + Set;
	}
	Buffer Where = "";
	isFirst = true;
	for (size_t i = 0; i < values.FieldDefine.size(); i++) {
		if (values.FieldDefine[i]->Condition & SQL_CONDITION) {
			if (!isFirst)Where += " AND ";
			else isFirst = false;
			Where += (Buffer)*values.FieldDefine[i] + " = " + values.FieldDefine[i]->toSqlStr();
		}
	}
	if (Where.size() > 0) {
		sql += " WHERE " + Where;
	}
	sql += ";";
	TRACEI("sql=%s", (const char*)sql);
	return sql;
}

Buffer _sqlite3_table_::Delete(const _Table_& values)
{//DELETE FROM 表全名 WHERE 条件;（条件可选）
	Buffer sql = "DELETE FROM " + (Buffer)*this;
	Buffer Where = "";
	bool isFirst = true;
	for (size_t i = 0; i < FieldDefine.size(); i++) {
		if (FieldDefine[i]->Condition & SQL_CONDITION) {
			if (!isFirst)Where += " AND ";
			else isFirst = false;
			Where += (Buffer)*FieldDefine[i] + " = " + FieldDefine[i]->toSqlStr();
		}
	}
	if (Where.size() > 0) {
		sql += " WHERE " + Where;
	}
	sql += ";";
	TRACEI("sql=%s", (const char*)sql);
	return sql;
}

Buffer _sqlite3_table_::Query(const Buffer& condition)
{//SELECT 列名1，列名2，... FROM 表全名 WHERE 条件;（条件可选）
	Buffer sql = "SELECT ";
	for (size_t i = 0; i < FieldDefine.size(); i++) {
		if (i > 0)sql += ",";
		sql += '"' + FieldDefine[i]->Name + "\" ";
	}
	sql += " FROM " + (Buffer)*this;
	if (condition.size() > 0) {
		sql += " WHERE " + condition;
	}
	sql += ";";
	TRACEI("sql=%s", (const char*)sql);
	return sql;
}

PTable _sqlite3_table_::Copy() const
{
	return PTable(new _sqlite3_table_(*this));
}

void _sqlite3_table_::ClearFieldUsed()
{
	for (size_t i = 0; i < FieldDefine.size(); i++) {
		FieldDefine[i]->Condition = 0;
	}
}

_sqlite3_table_::operator const Buffer() const
{
	Buffer head;
	if (DataBase.size())
		head  = '"'+DataBase+ "\".";
	return head + '"' + Name + '"';
}

_sqlite3_field_::_sqlite3_field_() : _Field_()
{
	nType = TYPE_NULL;
	Value.Double = 0.0;
}

_sqlite3_field_::_sqlite3_field_(int ntype, const Buffer& name, unsigned attr, const Buffer& type, const Buffer& size, const Buffer& default_, const Buffer& check)
{
	nType = ntype;
	switch (ntype)
	{
		case TYPE_VARCHAR:
		case TYPE_TEXT:
		case TYPE_BLOB:
			Value.String = new Buffer();
			break;
	}
	Name = name;
	Attr = attr;
	Type = type;
	Size = size;
	Default = default_;
	Check = check;
}

_sqlite3_field_::_sqlite3_field_(const _sqlite3_field_& field)
{
	nType = field.nType;
	switch (field.nType)
	{
	case TYPE_VARCHAR:
	case TYPE_TEXT:
	case TYPE_BLOB:
		Value.String = new Buffer();
		*Value.String = *field.Value.String;
		break;
	}
	Name = field.Name;
	Attr = field.Attr;
	Type = field.Type;
	Size = field.Size;
	Default = field.Default;
	Check = field.Check;
}

_sqlite3_field_::~_sqlite3_field_()
{
	switch (nType)
	{
	case TYPE_VARCHAR:
	case TYPE_TEXT:
	case TYPE_BLOB:
		if (Value.String) {
			Buffer* p = Value.String;
			Value.String = nullptr;
			delete p;
		}
		break;
	}
}

Buffer _sqlite3_field_::Create()
{//"名称" 类型 属性
	Buffer sql = '"' + Name + "\" " + Type + " ";
	if (Attr & NOT_NULL) {
		sql += "NOT NULL ";
	}
	if (Attr & DEFAULT) {
		sql += "DEFAULT " + Default + " ";
	}
	if (Attr & UNIQUE) {
		sql += "UNIQUE ";
	}
	if (Attr & PRIMARY_KEY) {
		sql += "PRIMARY KEY ";
	}
	if (Attr & CHECK) {
		sql += "CHECK( " + Check + ") ";
	}
	if (Attr & AUTOINCREMENT) {
		sql += "AUTOINCREMENT ";
	}	
	return sql;
}

void _sqlite3_field_::LoadFromStr(const Buffer& str)
{
	switch (nType)
	{
	case TYPE_NULL:
		break;
	case TYPE_BOOL:
	case TYPE_INT:
	case TYPE_DATETIME:
		Value.Integer = atoi(str);
		break;
	case TYPE_REAL:
		Value.Double = atof(str);
		break;
	case TYPE_VARCHAR:
	case TYPE_TEXT:
		*Value.String = str;
		break;
	case TYPE_BLOB:
		*Value.String = Str2Hex(str);
		break;
	default:
		TRACEW("unknown type %d", nType);
		break;
	}
}

Buffer _sqlite3_field_::toEqualExp() const
{
	Buffer sql = (Buffer)*this + " = ";
	std::stringstream ss;
	switch (nType)
	{
	case TYPE_NULL:
		sql+=" NULL ";
		break;
	case TYPE_BOOL:
	case TYPE_INT:
	case TYPE_DATETIME:
		ss << Value.Integer;
		sql += ss.str() + " ";
		break;
	case TYPE_REAL:
		ss << Value.Double;
		sql += ss.str() + " ";
		break;
	case TYPE_VARCHAR:
	case TYPE_TEXT:
	case TYPE_BLOB:
		sql += "\"" + *Value.String + "\" ";
		break;
	default:
		TRACEW("unknown type %d", nType);
		break;
	}
	return sql;
}

Buffer _sqlite3_field_::toSqlStr() const
{
	Buffer sql = "";
	std::stringstream ss;
	switch (nType)
	{
	case TYPE_NULL:
		sql += " NULL ";
		break;
	case TYPE_BOOL:
	case TYPE_INT:
	case TYPE_DATETIME:
		ss << Value.Integer;
		sql += ss.str() + " ";
		break;
	case TYPE_REAL:
		ss << Value.Double;
		sql += ss.str() + " ";
		break;
	case TYPE_VARCHAR:
	case TYPE_TEXT:
	case TYPE_BLOB:
		sql += "\"" + *Value.String + "\" ";
		break;
	default:
		TRACEW("unknown type %d", nType);
		break;
	}
	return sql;
}

_sqlite3_field_::operator const Buffer() const
{
	return '"' + Name + '"';
}

Buffer _sqlite3_field_::Str2Hex(const Buffer& data) const
{
	const char* hex = "0123456789ABCDEF";
	std::stringstream ss;
	for (auto ch : data) {
		ss << hex[(unsigned char)ch >> 4] << hex[(unsigned char)ch & 0xF];
	}
	return ss.str();
}

