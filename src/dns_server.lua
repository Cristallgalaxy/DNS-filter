local ngx = require "ngx"
local cjson = require "cjson"

-- 两张表：一个用于精确匹配，一个用于通配后缀匹配
local exact_domains = {}
local wildcard_suffixes = {}

-- 读取 domain.txt 文件
local file = io.open("/home/pingyuan/code/dns_parse/src/domain.txt", "r")
if file then
    for line in file:lines() do
        local domain = line:match("^%s*(.-)%s*$")
        if domain ~= "" then
            if domain:sub(1, 2) == "*." then
                -- "*.baidu.com" → 存储为 "baidu.com"
                table.insert(wildcard_suffixes, domain:sub(3))
            else
                exact_domains[domain] = true
            end
        end
    end
    file:close()
else
    ngx.say("[Error] Failed to open domain.txt")
    ngx.log(ngx.ERR, "[Error] Failed to open domain.txt")
end

-- 设置响应头
ngx.header.content_type = "text/plain"

-- 获取 POST 请求体
ngx.req.read_body()
local post_data = ngx.req.get_body_data()

if not post_data then
    ngx.say("[Response] No JSON data found in POST request")
    return ngx.exit(ngx.HTTP_OK)
end

local success, data = pcall(cjson.decode, post_data)
if not success then
    ngx.say("[Response] Failed to parse JSON")
    return ngx.exit(ngx.HTTP_OK)
end

-- ngx.say("[Received JSON]:")
-- ngx.say(post_data)
-- ngx.say("")

-- 检查是否匹配函数
local function is_domain_matched(domain)
    if exact_domains[domain] then
        return true
    end
    for _, suffix in ipairs(wildcard_suffixes) do
        -- 完全匹配或后缀匹配（且前面是点）
        if domain == suffix or domain:sub(-#suffix - 1) == "." .. suffix then
            return true
        end
    end
    return false
end

-- -- 处理域名
-- local domains = data.domains
-- if domains and type(domains) == "table" then
--     ngx.say("[Parsed Domains]:")
--     for _, domain in ipairs(domains) do
--         if is_domain_matched(domain) then
--             ngx.say(domain .. " [matched]")
--         else
--             ngx.say(domain)
--         end
--     end
-- else
--     ngx.say("[Response] No 'domains' array found in JSON")
-- end

local domains = data.domains
local permitted_set = {}
local drop_set = {}

if domains and type(domains) == "table" then
    for _, domain in ipairs(domains) do
        if is_domain_matched(domain) then
            permitted_set[domain] = true
        else
            drop_set[domain] = true
        end
    end

    -- 将 set 转换为去重后的数组
    local permitted_domains = {}
    for domain, _ in pairs(permitted_set) do
        table.insert(permitted_domains, domain)
    end

    local drop_domains = {}
    for domain, _ in pairs(drop_set) do
        table.insert(drop_domains, domain)
    end

    -- 构造 JSON 响应
    local response = {
        permitted = permitted_domains,
        dropped = drop_domains
    }

    -- ngx.say("\n[Response JSON]:")
    ngx.say(cjson.encode(response))
else
    ngx.say("[Response] No 'domains' array found in JSON")
end

return ngx.exit(ngx.HTTP_OK)
