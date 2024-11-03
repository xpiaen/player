#include "MysqlClient.h"
#include "Logger.h"
#include <sstream>

int CMysqlClient::Connect(const KeyValue& args)
{
    if (m_bInit)return -1;
    MYSQL* ret = mysql_init(&m_db);
    if (ret == nullptr)return -2;
    ret = mysql_real_connect(
        &m_db, args.at("host"), args.at("user"), args.at("password"), args.at("database"), atoi(args.at("port")), nullptr, 0
    );
    if (ret == nullptr || mysql_errno(&m_db) != 0) {
        printf("%s %d %s\n", __FUNCTION__,mysql_errno(&m_db), mysql_error(&m_db));
        TRACEE(" %d %s", mysql_errno(&m_db), mysql_error(&m_db));
        mysql_close(&m_db);
        bzero(&m_db, sizeof(m_db));
        return -3;
    }
    m_bInit = true;
    return 0;
}

int CMysqlClient::Exec(const Buffer& sql)
{
    if (!m_bInit)return -1;
    int ret = mysql_real_query(&m_db, sql, sql.size());
    if (ret != 0) {
        TRACEE(" %d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    return 0;
}

int CMysqlClient::Exec(const Buffer& sql, Result& result, const _Table_& table)
{
    if (!m_bInit)return -1;
    int ret = mysql_real_query(&m_db, sql, sql.size());
    if (ret != 0) {
        TRACEE(" %d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    MYSQL_RES* res = mysql_store_result(&m_db);
    MYSQL_ROW row;
    unsigned num_fields = mysql_num_fields(res);
    while ((row = mysql_fetch_row(res)) != nullptr) {
        PTable pt = table.Copy();
        for (unsigned i = 0; i < num_fields; i++) {
            if (row[i] != nullptr) {
                pt->FieldDefine[i]->LoadFromStr(row[i]);
            }
        }
        result.push_back(pt);
    }
    return 0;
}

int CMysqlClient::StartTransaction()
{
    if (!m_bInit)return -1;
    int ret = mysql_real_query(&m_db, "BEGIN", 6);
    if (ret != 0) {
        TRACEE(" %d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    return 0;
}

int CMysqlClient::CommitTransation()
{
    if (!m_bInit)return -1;
    int ret = mysql_real_query(&m_db, "COMMIT", 7);
    if (ret != 0) {
        TRACEE(" %d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    return 0;
}

int CMysqlClient::RollbackTransaction()
{
    if (!m_bInit)return -1;
    int ret = mysql_real_query(&m_db, "ROLLBACK", 9);
    if (ret != 0) {
        TRACEE(" %d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    return 0;
}

int CMysqlClient::Close()
{
    if (m_bInit) {
        m_bInit = false;
        mysql_close(&m_db);
        bzero(&m_db, sizeof(m_db));
    }
    return 0;
}

bool CMysqlClient::IsConnected()
{
    return m_bInit;
}


_mysql_table_::_mysql_table_(const _mysql_table_& table)
{
    DataBase = table.DataBase;
    Name = table.Name;
    for (size_t i = 0; i < table.FieldDefine.size(); i++) {
        PField field = PField(new _mysql_field_(*(_mysql_field_*)table.FieldDefine[i].get()));
        FieldDefine.push_back(field);
        Fields[field->Name] = field;
    }
}

_mysql_table_::~_mysql_table_()
{}

Buffer _mysql_table_::Create()
{   //CREATE TABLE IF NOT EXISTS 表全名（列定义，...,
    //PRIMARY KEY `主键列名`，UNIQUE INDEX `列名_UNIQUE`(列名ASC)VISIBLE)
    Buffer sql = "CREATE TABLE IF NOT EXISTS "+(Buffer)*this + " (\r\n";
    for (unsigned i = 0; i < FieldDefine.size(); i++)
    {
        if(i>0)sql +=",\r\n";
        sql += FieldDefine[i]->Create();
        if (FieldDefine[i]->Attr & PRIMARY_KEY) {
            sql += ",\r\nPRIMARY KEY (`" + FieldDefine[i]->Name + "`)";
        }
        if (FieldDefine[i]->Attr & UNIQUE) {
            sql += ",\r\n UNIQUE INDEX `" + FieldDefine[i]->Name + "_UNIQUE` (" + (Buffer)*FieldDefine[i] + " ASC) VISIBLE ";
        }
    }
    sql += ");";
    return sql;
}

Buffer _mysql_table_::Drop()
{//DROP TABLE IF EXISTS 表全名
    return "DROP TABLE IF EXISTS "+(Buffer)*this+";";
}

Buffer _mysql_table_::Insert(const _Table_& values)
{//INSERT INTO 表全名(列名1,...,列名n) VALUES(值1,...,值n)
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
        if (values.FieldDefine[i]->Condition & SQL_INSERT) {
            if (!isFirst)sql += ",";
            else isFirst = false;
            sql += values.FieldDefine[i]->toSqlStr();
        }
    }
    sql += ");";
    TRACEI("sql=%s", (const char*)sql);
    return sql;
}

Buffer _mysql_table_::Modify(const _Table_& values)
{
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

Buffer _mysql_table_::Delete(const _Table_& values)
{
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

Buffer _mysql_table_::Query(const Buffer& condition)
{
    Buffer sql = "SELECT ";
    for (size_t i = 0; i < FieldDefine.size(); i++) {
        if (i > 0)sql += ",";
        sql += '`' + FieldDefine[i]->Name + "` ";
    }
    sql += " FROM " + (Buffer)*this;
    if (condition.size() > 0) {
        sql += " WHERE " + condition;
    }
    sql += ";";
    TRACEI("sql=%s", (const char*)sql);
    return sql;
}

PTable _mysql_table_::Copy() const
{
    return PTable(new _mysql_table_(*this));
}

void _mysql_table_::ClearFieldUsed()
{
    for (size_t i = 0; i < FieldDefine.size(); i++) {
        FieldDefine[i]->Condition = 0;
    }
}

_mysql_table_::operator const Buffer() const
{
    Buffer head;
    if (DataBase.size())
        head = '`' + DataBase + "`.";
    return head + '`' + Name + '`';
}

_mysql_field_::_mysql_field_():_Field_()
{
    nType = TYPE_NULL;
    Value.Double = 0.0;
}

_mysql_field_::_mysql_field_(int ntype, const Buffer& name, unsigned attr, const Buffer& type, const Buffer& size, const Buffer& default_, const Buffer& check)
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

_mysql_field_::_mysql_field_(const _mysql_field_& field)
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

_mysql_field_::~_mysql_field_()
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

Buffer _mysql_field_::Create()
{
    Buffer sql = "`" + Name + "` " + Type + Size + " ";
    if (Attr & NOT_NULL) {
        sql += "NOT NULL ";
    }
    else {
        sql += "NULL ";
    }
    //BLOB TEXT GEOMETRY JSON 类型不能有默认值(DEFAULT)
    if ((Attr & DEFAULT) && (Default.size()) && (Type != "BLOB") && (Type != "TEXT") && (Type != "GEOMETRY") && (Type != "JSON")) {
        sql+= "DEFAULT \"" + Default + "\" ";
    }
    //UNIQUE PRIMARY_KEY 外面处理
    //CHECK mysql不支持
    if (Attr & AUTOINCREMENT) {
        sql += "AUTO_INCREMENT ";
    }
    return sql;
}

void _mysql_field_::LoadFromStr(const Buffer& str)
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
        //TRACEW("unknown type %d", nType);
        printf("unknown type %d\n", nType);
        break;
    }
}

Buffer _mysql_field_::toEqualExp() const
{
    Buffer sql = (Buffer)*this + " = ";
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
        //TRACEW("unknown type %d", nType);
        printf("unknown type %d\n", nType);
        break;
    }
    return sql;
}

Buffer _mysql_field_::toSqlStr() const
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
        //TRACEW("unknown type %d", nType);
        printf("unknown type %d\n", nType);
        break;
    }
    return sql;
}
   

_mysql_field_::operator const Buffer() const
{
    return '`' + Name + '`';
}

Buffer _mysql_field_::Str2Hex(const Buffer& data) const
{
    const char* hex = "0123456789ABCDEF";
    std::stringstream ss;
    for (auto ch : data) {
        ss << hex[(unsigned char)ch >> 4] << hex[(unsigned char)ch & 0xF];
    }
    return ss.str();
}
