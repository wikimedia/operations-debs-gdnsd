@	SOA ns1 dns-admin (
	1      ; serial
	7200   ; refresh
	1800   ; retry
	259200 ; expire
        900    ; ncache
)

@	NS	ns1
ns1	A	192.0.2.1

res1	DYNA	geoip!res1
res1sf	DYNA	geoip!res1sf
res1na	DYNA	geoip!res1/na
res1sa	DYNA	geoip!res1/sa
res1eu	DYNA	geoip!res1/eu
res1ap	DYNA	geoip!res1/ap

res2	DYNA	geoip!res2
res2dc1	DYNA	geoip!res2/dc1
res2dc2	DYNA	geoip!res2/dc2
res2dc3	DYNA	geoip!res2/dc3

res3	DYNC	geoip!res3
res3dc1	DYNC	geoip!res3/dc1
res3dc2	DYNC	geoip!res3/dc2
res3dc3	DYNC	geoip!res3/dc3

dc1cn	A	192.0.2.100
dc2cn	AAAA	2001:DB8::101
dc3cn	A	192.0.2.102
dc3cn	AAAA	2001:DB8::102

dmx	MX	0 res1
dmx	MX	1 res2
dmx	MX	2 res3

res4-lots	DYNA	geoip!res4
res4-lots-dync	DYNC	geoip!res4
res44-lots	DYNA	geoip!res44
res44-lots-dync	DYNC	geoip!res44

res5	DYNC	geoip!res5
dc2cn-loop CNAME res5

res6	DYNA	geoip!res6
res7	DYNC	geoip!res7
res8	DYNA	geoip!res8
res9	DYNA	geoip!res9
resA	DYNA	geoip!resA

metascope DYNA metafo!metascope

res-empty-a DYNA geoip!res-e
res-empty-c DYNC geoip!res-e

res-undef DYNA geoip!res-u

*.res-w DYNA geoip!res-w

res-noecs DYNA geoip!res-noecs
